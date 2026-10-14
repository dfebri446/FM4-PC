#pragma once
// Optional per-second summary of where the GPU command processor thread spends
// its time, written as JSON lines to REX_PERF_TRACE_FILE. Diagnostic only: when
// the variable is unset every helper returns immediately.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace rex::graphics::perf_trace {

enum class Wait : uint32_t {
  kGuestIdle,        // Command processor waiting for guest commands (CPU-bound).
  kFrameLatency,     // Waiting for an old frame before opening a new one (GPU-bound).
  kResolveReadback,  // Synchronous resolve readback to guest memory.
  kOcclusion,        // Occlusion query results.
  kMemexport,        // Memexport readback.
  kOther,
  kCount
};

// Event counters per window, to judge which stalls are worth removing.
enum class Count : uint32_t {
  kOcclusionAwaited,    // ROV occlusion snapshot for a report the guest spins on.
  kOcclusionUnawaited,  // ROV occlusion snapshot nobody waits for yet.
  kVizQueries,          // PM4_VIZ_QUERY begin.
  kVizDraws,            // Draw conditional on a viz query (viz_use bit).
  kBinSelects,          // SET_BIN_SELECT(_LO); HI is not counted to avoid counting pairs twice.
  kPredicatedRun,       // Predicated type-3 packet whose bin check passed.
  kPredicatedSkipped,   // Predicated type-3 packet skipped by the bin check.
  kPipelineDrawsWaited,   // Async shaders: draw waited for its pipeline, then drawn.
  kPipelineDrawsDropped,  // Async shaders: draw dropped, pipeline not ready.
  kSubmissions,           // Command lists submitted to the GPU queue.
  kCount
};

// Wall time of command-processor work, inclusive (kDraw contains the kDraw*
// parts; kSubmit may run inside kDraw or kCopy).
enum class Zone : uint32_t {
  kDraw,
  kDrawPrimitives,
  kDrawRenderTargets,
  kDrawPipeline,
  kDrawTextures,
  kDrawBindings,
  kDrawVertexBuffers,
  kCopy,
  kSubmit,
  kSwap,
  kSharedMemoryLock,    // Acquiring the global lock in SharedMemory::RequestRanges.
  kSharedMemoryUpload,  // Uploading invalidated guest memory to the GPU buffer.
  kSubmitRecord,        // Replaying the deferred command list into the D3D12 one.
  kSubmitExecute,       // Close + ExecuteCommandLists + Signal.
  kCount
};

struct State {
  std::ofstream output;
  Wait cause = Wait::kOther;
  uint64_t frequency = 0, origin = 0, window_start = 0, last_swap = 0;
  uint64_t max_frame = 0, readback_bytes = 0;
  uint32_t swaps = 0, draws = 0, copies = 0, readbacks = 0;
  uint64_t wait_ticks[size_t(Wait::kCount)]{};
  uint32_t waits[size_t(Wait::kCount)]{};
  uint32_t counts[size_t(Count::kCount)]{};
  uint64_t zone_ticks[size_t(Zone::kCount)]{};
  std::vector<float> frame_ms;
  // GPU timestamps of frames that completed in this window.
  uint32_t gpu_frames = 0;
  double gpu_busy_ms = 0, gpu_resolve_ms = 0, gpu_span_ms = 0;
  // Samples passing depth/stencil per occlusion snapshot interval, as reported
  // to the guest (after any divisor), to compare paths and calibrate.
  double occlusion_samples[2]{};  // Unawaited, awaited.
  uint32_t occlusion_intervals[2]{};
};

// Every resolve destination seen in the run, rewritten as JSON next to the
// trace (<REX_PERF_TRACE_FILE>.resolves.json) every 10 windows. Used to find
// which resolves the CPU actually needs.
struct ResolveInventory {
  struct Entry {
    uint64_t count = 0;
    double first_s = 0, last_s = 0;
  };
  // address, length, format, pitch, height.
  std::map<std::array<uint32_t, 5>, Entry> entries;
  std::string path;
  uint32_t windows = 0;
};

inline uint64_t Now() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

inline State* Get() {
  static State* state = []() -> State* {
    const char* path = std::getenv("REX_PERF_TRACE_FILE");
    if (!path || !*path) return nullptr;
    auto* result = new State;
    result->output.open(path, std::ios::app);
    if (!result->output.is_open()) return nullptr;
    result->frequency = 1000000000;
    result->origin = result->window_start = Now();
    return result;
  }();
  return state;
}

// Attributes fence waits inside its scope to a cause.
class CauseScope {
 public:
  explicit CauseScope(Wait cause) : state_(Get()) {
    if (!state_) return;
    previous_ = state_->cause;
    state_->cause = cause;
  }
  ~CauseScope() {
    if (state_) state_->cause = previous_;
  }

 private:
  State* state_;
  Wait previous_ = Wait::kOther;
};

inline void AddWait(uint64_t start, Wait cause) {
  State* state = Get();
  if (!state) return;
  state->wait_ticks[size_t(cause)] += Now() - start;
  ++state->waits[size_t(cause)];
}
inline void AddFenceWait(uint64_t start) {
  if (State* state = Get()) AddWait(start, state->cause);
}
inline void CountDraw() {
  if (State* state = Get()) ++state->draws;
}
inline void CountCopy() {
  if (State* state = Get()) ++state->copies;
}
inline void CountReadback(uint32_t bytes) {
  if (State* state = Get()) {
    ++state->readbacks;
    state->readback_bytes += bytes;
  }
}

inline void Increment(Count counter) {
  if (State* state = Get()) ++state->counts[size_t(counter)];
}

inline uint64_t ZoneStart() { return Get() ? Now() : 0; }
inline void ZoneEnd(Zone zone, uint64_t start) {
  if (State* state = Get(); state && start) state->zone_ticks[size_t(zone)] += Now() - start;
}
class ZoneScope {
 public:
  explicit ZoneScope(Zone zone) : zone_(zone), start_(ZoneStart()) {}
  ~ZoneScope() { ZoneEnd(zone_, start_); }

 private:
  Zone zone_;
  uint64_t start_;
};

inline void AddOcclusionInterval(bool awaited, uint64_t passed_samples) {
  State* state = Get();
  if (!state) return;
  state->occlusion_samples[awaited] += double(passed_samples);
  ++state->occlusion_intervals[awaited];
}

// busy: sum of submission durations; resolve: sum of resolve durations; span:
// first to last GPU timestamp of the frame, including idle gaps.
inline void AddGpuFrame(double busy_ms, double resolve_ms, double span_ms) {
  State* state = Get();
  if (!state) return;
  ++state->gpu_frames;
  state->gpu_busy_ms += busy_ms;
  state->gpu_resolve_ms += resolve_ms;
  state->gpu_span_ms += span_ms;
}

inline ResolveInventory* GetResolveInventory() {
  static ResolveInventory* inventory = []() -> ResolveInventory* {
    if (!Get()) return nullptr;
    auto* result = new ResolveInventory;
    result->path = std::string(std::getenv("REX_PERF_TRACE_FILE")) + ".resolves.json";
    return result;
  }();
  return inventory;
}

inline void CountResolve(uint32_t address, uint32_t length, uint32_t format, uint32_t pitch,
                         uint32_t height) {
  ResolveInventory* inventory = GetResolveInventory();
  if (!inventory || !length) return;
  const double seconds = double(Now() - Get()->origin) / 1e9;
  auto& entry = inventory->entries[{address, length, format, pitch, height}];
  if (!entry.count++) entry.first_s = seconds;
  entry.last_s = seconds;
}

inline void WriteResolveInventory(double seconds) {
  ResolveInventory* inventory = GetResolveInventory();
  if (!inventory) return;
  std::ofstream output(inventory->path, std::ios::trunc);
  output << "{\"seconds\":" << seconds << ",\"resolves\":[";
  bool first = true;
  for (const auto& [key, entry] : inventory->entries) {
    output << (first ? "" : ",") << "\n{\"address\":" << key[0] << ",\"length\":" << key[1]
           << ",\"format\":" << key[2] << ",\"pitch\":" << key[3] << ",\"height\":" << key[4]
           << ",\"count\":" << entry.count << ",\"first_s\":" << entry.first_s
           << ",\"last_s\":" << entry.last_s << "}";
    first = false;
  }
  output << "]}\n";
}

inline void OnSwap() {
  State* state = Get();
  if (!state) return;
  const uint64_t now = Now();
  if (state->last_swap) {
    const uint64_t frame = now - state->last_swap;
    state->max_frame = frame > state->max_frame ? frame : state->max_frame;
    state->frame_ms.push_back(float(double(frame) / 1e6));
  }
  state->last_swap = now;
  ++state->swaps;
  const uint64_t elapsed = now - state->window_start;
  if (elapsed < state->frequency) return;
  const auto ms = [&](uint64_t ticks) { return double(ticks) * 1000.0 / double(state->frequency); };
  static const char* const kNames[] = {"guest_idle", "frame_latency", "resolve_readback",
                                       "occlusion", "memexport", "other"};
  state->output << "{\"t\":" << ms(now - state->origin) / 1000.0
                << ",\"window_ms\":" << ms(elapsed)
                << ",\"fps\":" << double(state->swaps) * 1000.0 / ms(elapsed)
                << ",\"frame_max_ms\":" << ms(state->max_frame) << ",\"draws\":" << state->draws
                << ",\"copies\":" << state->copies << ",\"readbacks\":" << state->readbacks
                << ",\"readback_kb\":" << (state->readback_bytes >> 10) << ",\"wait_ms\":{";
  for (size_t i = 0; i < size_t(Wait::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kNames[i] << "\":" << ms(state->wait_ticks[i]);
  }
  state->output << "},\"waits\":{";
  for (size_t i = 0; i < size_t(Wait::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kNames[i] << "\":" << state->waits[i];
  }
  static const char* const kCountNames[] = {"occlusion_awaited", "occlusion_unawaited",
                                            "viz_queries",       "viz_draws",
                                            "bin_selects",       "predicated_run",
                                            "predicated_skipped", "pipeline_draws_waited",
                                            "pipeline_draws_dropped", "submissions"};
  state->output << "},\"counts\":{";
  for (size_t i = 0; i < size_t(Count::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kCountNames[i] << "\":" << state->counts[i];
  }
  static const char* const kZoneNames[] = {"draw",          "draw_primitives",
                                           "draw_render_targets", "draw_pipeline",
                                           "draw_textures", "draw_bindings",
                                           "draw_vertex_buffers", "copy",
                                           "submit",        "swap",
                                           "shared_memory_lock", "shared_memory_upload",
                                           "submit_record", "submit_execute"};
  state->output << "},\"cpu_ms\":{";
  for (size_t i = 0; i < size_t(Zone::kCount); ++i) {
    state->output << (i ? "," : "") << '"' << kZoneNames[i] << "\":" << ms(state->zone_ticks[i]);
  }
  // Mean passing samples per occlusion interval.
  const auto mean_samples = [&](size_t i) {
    return state->occlusion_intervals[i] ? state->occlusion_samples[i] / state->occlusion_intervals[i]
                                         : 0.0;
  };
  state->output << "},\"occlusion_samples\":{\"unawaited\":" << mean_samples(0)
                << ",\"awaited\":" << mean_samples(1);
  // Means per GPU frame, from timestamps read a few frames late.
  const double gpu_frames = state->gpu_frames ? double(state->gpu_frames) : 1.0;
  state->output << "},\"gpu\":{\"frames\":" << state->gpu_frames
                << ",\"frame_ms\":" << state->gpu_busy_ms / gpu_frames
                << ",\"resolve_ms\":" << state->gpu_resolve_ms / gpu_frames
                << ",\"span_ms\":" << state->gpu_span_ms / gpu_frames;
  // Every frame duration in the window, for run-wide percentiles and hitches.
  state->output << "},\"frame_ms\":[";
  for (size_t i = 0; i < state->frame_ms.size(); ++i) {
    state->output << (i ? "," : "") << state->frame_ms[i];
  }
  state->output << "]}\n";
  state->output.flush();
  if (ResolveInventory* inventory = GetResolveInventory(); inventory && ++inventory->windows % 10 == 0) {
    WriteResolveInventory(ms(now - state->origin) / 1000.0);
  }
  const Wait cause = state->cause;
  std::ofstream output = std::move(state->output);
  std::vector<float> frame_ms = std::move(state->frame_ms);
  frame_ms.clear();
  const uint64_t frequency = state->frequency, origin = state->origin;
  *state = State{};
  state->output = std::move(output);
  state->frame_ms = std::move(frame_ms);
  state->cause = cause;
  state->frequency = frequency;
  state->origin = origin;
  state->window_start = state->last_swap = now;
}

}  // namespace rex::graphics::perf_trace
