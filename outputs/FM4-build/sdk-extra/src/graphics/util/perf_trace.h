#pragma once
// Optional per-second summary of where the GPU command processor thread spends
// its time, written as JSON lines to REX_PERF_TRACE_FILE. Diagnostic only: when
// the variable is unset every helper returns immediately.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>

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

struct State {
  std::ofstream output;
  Wait cause = Wait::kOther;
  uint64_t frequency = 0, origin = 0, window_start = 0, last_swap = 0;
  uint64_t max_frame = 0, readback_bytes = 0;
  uint32_t swaps = 0, draws = 0, copies = 0, readbacks = 0;
  uint64_t wait_ticks[size_t(Wait::kCount)]{};
  uint32_t waits[size_t(Wait::kCount)]{};
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

inline void OnSwap() {
  State* state = Get();
  if (!state) return;
  const uint64_t now = Now();
  if (state->last_swap && now - state->last_swap > state->max_frame) {
    state->max_frame = now - state->last_swap;
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
  state->output << "}}\n";
  state->output.flush();
  const Wait cause = state->cause;
  std::ofstream output = std::move(state->output);
  const uint64_t frequency = state->frequency, origin = state->origin;
  *state = State{};
  state->output = std::move(output);
  state->cause = cause;
  state->frequency = frequency;
  state->origin = origin;
  state->window_start = state->last_swap = now;
}

}  // namespace rex::graphics::perf_trace
