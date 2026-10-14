// Optional call counts of the guest functions that build PM4 packets
// (FM4_D3D_TRACE=<path>), for the Stage 3.1 inventory of the title's D3D layer.
// The list comes from a static scan of the generated code for type-3 packet
// headers (addresses and packet names only). Every 5 seconds the file is
// rewritten with "<function> <packets> <calls> <caller>:<calls> ..." lines,
// callers being guest return addresses (top 8).
#include <rex/hook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Function {
  uint32_t address;
  const char* packets;
};
constexpr Function kFunctions[] = {
    {0x822A7E08, "EVENT_WRITE_ZPD,SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x822A7FD0, "EVENT_WRITE,EVENT_WRITE_EXT,MEM_WRITE,WAIT_REG_MEM"},
    {0x822ACC80, "WAIT_REG_MEM"},
    {0x822B2FB8, "WAIT_REG_MEM"},
    {0x822BF380, "WAIT_REG_MEM"},
    {0x822C22A8, "WAIT_REG_MEM"},
    {0x822DADA8, "INDIRECT_BUFFER,INDIRECT_BUFFER_PFD"},
    {0x822DB108, "WAIT_REG_MEM"},
    {0x822EBE20, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE,SET_BIN_MASK_LO"},
    {0x82310530, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x82310818, "SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_BIN_SELECT_HI"},
    {0x82311080, "SET_BIN_MASK_LO"},
    {0x82333538, "SET_BIN_MASK_LO"},
    {0x8233AC60, "SET_BIN_MASK_LO"},
    {0x82343608, "LOAD_ALU_CONSTANT"},
    {0x823436B8, "IM_LOAD,SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x82344038, "EVENT_WRITE"},
    {0x82348DC8, "EVENT_WRITE,IM_LOAD_IMMEDIATE,INVALIDATE_STATE,SET_BIN_MASK_HI,SET_BIN_MASK_LO,WAIT_REG_MEM"},
    {0x8234D278, "SET_BIN_MASK_LO"},
    {0x8234DAE8, "SET_BIN_MASK_LO"},
    {0x823619B0, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x82361D40, "SET_BIN_MASK_LO"},
    {0x8236F6F8, "EVENT_WRITE_SHD,INVALIDATE_STATE,NOP"},
    {0x823712F8, "SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_BIN_SELECT_HI"},
    {0x82374660, "EVENT_WRITE"},
    {0x8237FA28, "NOP,REG_RMW"},
    {0x82381318, "EVENT_WRITE"},
    {0x82382A70, "SET_BIN_MASK_HI,SET_BIN_MASK_LO,SET_BIN_SELECT_HI,SET_BIN_SELECT_LO"},
    {0x82386C48, "WAIT_REG_MEM"},
    {0x82392800, "NOP"},
    {0x82395A48, "EVENT_WRITE_SHD"},
    {0x8239DF28, "REG_RMW"},
    {0x826E1550, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x826E35F8, "SET_BIN_MASK_LO,WAIT_REG_MEM"},
    {0x826E3CA8, "SET_BIN_SELECT_HI"},
    {0x826E3E60, "SET_BIN_SELECT_HI"},
    {0x826E4E28, "ME_INIT"},
    {0x826E5598, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E5C20, "WAIT_REG_MEM"},
    {0x826E6490, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E6D70, "DRAW_INDX_2"},
    {0x826E7220, "WAIT_REG_MEM"},
    {0x826E75C0, "DRAW_INDX_2,IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E7718, "DRAW_INDX_2,IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E7AA0, "EVENT_WRITE"},
    {0x826E7B98, "EVENT_WRITE"},
    {0x826E80A8, "WAIT_REG_MEM"},
    {0x826E8928, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826E8C80, "VIZ_QUERY"},
    {0x826E8D60, "VIZ_QUERY"},
    {0x826E8E30, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x826E8FD8, "SET_BIN_MASK_HI,SET_BIN_MASK_LO"},
    {0x826E92A0, "REG_RMW"},
    {0x826E9CF0, "WAIT_FOR_IDLE"},
    {0x826EBBC0, "COND_WRITE"},
    {0x826EBCB0, "COND_WRITE"},
    {0x826EC9C0, "IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826ECF30, "SET_BIN_SELECT_LO"},
    {0x826ED400, "EVENT_WRITE_SHD"},
    {0x826EE8E8, "INVALIDATE_STATE"},
    {0x826EE940, "IM_LOAD_IMMEDIATE"},
    {0x826EEBC0, "DRAW_INDX_2"},
    {0x826F1390, "EVENT_WRITE_SHD"},
    {0x826F3228, "INDIRECT_BUFFER"},
    {0x826F3660, "EVENT_WRITE_SHD"},
    {0x826F36C8, "SET_BIN_SELECT_HI"},
    {0x826F3858, "MEM_WRITE"},
    {0x826F64A8, "EVENT_WRITE,IM_LOAD_IMMEDIATE,INVALIDATE_STATE"},
    {0x826F7FB0, "REG_RMW"},
};
constexpr size_t kCount = sizeof(kFunctions) / sizeof(kFunctions[0]);

struct Trace {
  std::string path;
  std::array<std::atomic<uint64_t>, kCount> calls{};
  std::mutex mutex;
  std::map<uint64_t, uint64_t> callers;  // function index << 32 | return address
};

void Write(Trace& trace, double seconds) {
  std::map<uint64_t, uint64_t> callers;
  {
    std::lock_guard<std::mutex> lock(trace.mutex);
    callers = trace.callers;
  }
  std::ofstream output(trace.path + ".tmp");
  output << "# seconds " << seconds << "\n";
  for (size_t i = 0; i < kCount; ++i) {
    const uint64_t calls = trace.calls[i].load(std::memory_order_relaxed);
    if (!calls) continue;
    std::vector<std::pair<uint64_t, uint32_t>> top;
    for (auto it = callers.lower_bound(uint64_t(i) << 32);
         it != callers.end() && (it->first >> 32) == i; ++it) {
      top.emplace_back(it->second, uint32_t(it->first));
    }
    std::sort(top.rbegin(), top.rend());
    char head[96];
    std::snprintf(head, sizeof(head), "sub_%08X %s %llu", kFunctions[i].address, kFunctions[i].packets,
                  static_cast<unsigned long long>(calls));
    output << head;
    for (size_t k = 0; k < top.size() && k < 8; ++k) {
      char caller[48];
      std::snprintf(caller, sizeof(caller), " %08X:%llu", top[k].second,
                    static_cast<unsigned long long>(top[k].first));
      output << caller;
    }
    output << "\n";
  }
  output.close();
  std::error_code error;
  std::filesystem::rename(trace.path + ".tmp", trace.path, error);
}

Trace* GetTrace() {
  static Trace* trace = [] {
    const char* path = std::getenv("FM4_D3D_TRACE");
    if (!path || !*path) return static_cast<Trace*>(nullptr);
    auto* result = new Trace;
    result->path = path;
    std::thread([result] {
      const auto start = std::chrono::steady_clock::now();
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        Write(*result, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
      }
    }).detach();
    return result;
  }();
  return trace;
}

inline void Count(size_t index, const PPCContext& ctx) {
  Trace* trace = GetTrace();
  if (!trace) return;
  trace->calls[index].fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(trace->mutex);
  ++trace->callers[uint64_t(index) << 32 | uint32_t(ctx.lr)];
}
}  // namespace

REX_EXTERN(__imp__sub_822A7E08);
REX_HOOK_RAW(sub_822A7E08) { Count(0, ctx); __imp__sub_822A7E08(ctx, base); }
REX_EXTERN(__imp__sub_822A7FD0);
REX_HOOK_RAW(sub_822A7FD0) { Count(1, ctx); __imp__sub_822A7FD0(ctx, base); }
REX_EXTERN(__imp__sub_822ACC80);
REX_HOOK_RAW(sub_822ACC80) { Count(2, ctx); __imp__sub_822ACC80(ctx, base); }
REX_EXTERN(__imp__sub_822B2FB8);
REX_HOOK_RAW(sub_822B2FB8) { Count(3, ctx); __imp__sub_822B2FB8(ctx, base); }
REX_EXTERN(__imp__sub_822BF380);
REX_HOOK_RAW(sub_822BF380) { Count(4, ctx); __imp__sub_822BF380(ctx, base); }
REX_EXTERN(__imp__sub_822C22A8);
REX_HOOK_RAW(sub_822C22A8) { Count(5, ctx); __imp__sub_822C22A8(ctx, base); }
REX_EXTERN(__imp__sub_822DADA8);
REX_HOOK_RAW(sub_822DADA8) { Count(6, ctx); __imp__sub_822DADA8(ctx, base); }
REX_EXTERN(__imp__sub_822DB108);
REX_HOOK_RAW(sub_822DB108) { Count(7, ctx); __imp__sub_822DB108(ctx, base); }
REX_EXTERN(__imp__sub_822EBE20);
REX_HOOK_RAW(sub_822EBE20) { Count(8, ctx); __imp__sub_822EBE20(ctx, base); }
REX_EXTERN(__imp__sub_82310530);
REX_HOOK_RAW(sub_82310530) { Count(9, ctx); __imp__sub_82310530(ctx, base); }
REX_EXTERN(__imp__sub_82310818);
REX_HOOK_RAW(sub_82310818) { Count(10, ctx); __imp__sub_82310818(ctx, base); }
REX_EXTERN(__imp__sub_82311080);
REX_HOOK_RAW(sub_82311080) { Count(11, ctx); __imp__sub_82311080(ctx, base); }
REX_EXTERN(__imp__sub_82333538);
REX_HOOK_RAW(sub_82333538) { Count(12, ctx); __imp__sub_82333538(ctx, base); }
REX_EXTERN(__imp__sub_8233AC60);
REX_HOOK_RAW(sub_8233AC60) { Count(13, ctx); __imp__sub_8233AC60(ctx, base); }
REX_EXTERN(__imp__sub_82343608);
REX_HOOK_RAW(sub_82343608) { Count(14, ctx); __imp__sub_82343608(ctx, base); }
REX_EXTERN(__imp__sub_823436B8);
REX_HOOK_RAW(sub_823436B8) { Count(15, ctx); __imp__sub_823436B8(ctx, base); }
REX_EXTERN(__imp__sub_82344038);
REX_HOOK_RAW(sub_82344038) { Count(16, ctx); __imp__sub_82344038(ctx, base); }
REX_EXTERN(__imp__sub_82348DC8);
REX_HOOK_RAW(sub_82348DC8) { Count(17, ctx); __imp__sub_82348DC8(ctx, base); }
REX_EXTERN(__imp__sub_8234D278);
REX_HOOK_RAW(sub_8234D278) { Count(18, ctx); __imp__sub_8234D278(ctx, base); }
REX_EXTERN(__imp__sub_8234DAE8);
REX_HOOK_RAW(sub_8234DAE8) { Count(19, ctx); __imp__sub_8234DAE8(ctx, base); }
REX_EXTERN(__imp__sub_823619B0);
REX_HOOK_RAW(sub_823619B0) { Count(20, ctx); __imp__sub_823619B0(ctx, base); }
REX_EXTERN(__imp__sub_82361D40);
REX_HOOK_RAW(sub_82361D40) { Count(21, ctx); __imp__sub_82361D40(ctx, base); }
REX_EXTERN(__imp__sub_8236F6F8);
REX_HOOK_RAW(sub_8236F6F8) { Count(22, ctx); __imp__sub_8236F6F8(ctx, base); }
REX_EXTERN(__imp__sub_823712F8);
REX_HOOK_RAW(sub_823712F8) { Count(23, ctx); __imp__sub_823712F8(ctx, base); }
REX_EXTERN(__imp__sub_82374660);
REX_HOOK_RAW(sub_82374660) { Count(24, ctx); __imp__sub_82374660(ctx, base); }
REX_EXTERN(__imp__sub_8237FA28);
REX_HOOK_RAW(sub_8237FA28) { Count(25, ctx); __imp__sub_8237FA28(ctx, base); }
REX_EXTERN(__imp__sub_82381318);
REX_HOOK_RAW(sub_82381318) { Count(26, ctx); __imp__sub_82381318(ctx, base); }
REX_EXTERN(__imp__sub_82382A70);
REX_HOOK_RAW(sub_82382A70) { Count(27, ctx); __imp__sub_82382A70(ctx, base); }
REX_EXTERN(__imp__sub_82386C48);
REX_HOOK_RAW(sub_82386C48) { Count(28, ctx); __imp__sub_82386C48(ctx, base); }
REX_EXTERN(__imp__sub_82392800);
REX_HOOK_RAW(sub_82392800) { Count(29, ctx); __imp__sub_82392800(ctx, base); }
REX_EXTERN(__imp__sub_82395A48);
REX_HOOK_RAW(sub_82395A48) { Count(30, ctx); __imp__sub_82395A48(ctx, base); }
REX_EXTERN(__imp__sub_8239DF28);
REX_HOOK_RAW(sub_8239DF28) { Count(31, ctx); __imp__sub_8239DF28(ctx, base); }
REX_EXTERN(__imp__sub_826E1550);
REX_HOOK_RAW(sub_826E1550) { Count(32, ctx); __imp__sub_826E1550(ctx, base); }
REX_EXTERN(__imp__sub_826E35F8);
REX_HOOK_RAW(sub_826E35F8) { Count(33, ctx); __imp__sub_826E35F8(ctx, base); }
REX_EXTERN(__imp__sub_826E3CA8);
REX_HOOK_RAW(sub_826E3CA8) { Count(34, ctx); __imp__sub_826E3CA8(ctx, base); }
REX_EXTERN(__imp__sub_826E3E60);
REX_HOOK_RAW(sub_826E3E60) { Count(35, ctx); __imp__sub_826E3E60(ctx, base); }
REX_EXTERN(__imp__sub_826E4E28);
REX_HOOK_RAW(sub_826E4E28) { Count(36, ctx); __imp__sub_826E4E28(ctx, base); }
REX_EXTERN(__imp__sub_826E5598);
REX_HOOK_RAW(sub_826E5598) { Count(37, ctx); __imp__sub_826E5598(ctx, base); }
REX_EXTERN(__imp__sub_826E5C20);
REX_HOOK_RAW(sub_826E5C20) { Count(38, ctx); __imp__sub_826E5C20(ctx, base); }
REX_EXTERN(__imp__sub_826E6490);
REX_HOOK_RAW(sub_826E6490) { Count(39, ctx); __imp__sub_826E6490(ctx, base); }
REX_EXTERN(__imp__sub_826E6D70);
REX_HOOK_RAW(sub_826E6D70) { Count(40, ctx); __imp__sub_826E6D70(ctx, base); }
REX_EXTERN(__imp__sub_826E7220);
REX_HOOK_RAW(sub_826E7220) { Count(41, ctx); __imp__sub_826E7220(ctx, base); }
REX_EXTERN(__imp__sub_826E75C0);
REX_HOOK_RAW(sub_826E75C0) { Count(42, ctx); __imp__sub_826E75C0(ctx, base); }
REX_EXTERN(__imp__sub_826E7718);
REX_HOOK_RAW(sub_826E7718) { Count(43, ctx); __imp__sub_826E7718(ctx, base); }
REX_EXTERN(__imp__sub_826E7AA0);
REX_HOOK_RAW(sub_826E7AA0) { Count(44, ctx); __imp__sub_826E7AA0(ctx, base); }
REX_EXTERN(__imp__sub_826E7B98);
REX_HOOK_RAW(sub_826E7B98) { Count(45, ctx); __imp__sub_826E7B98(ctx, base); }
REX_EXTERN(__imp__sub_826E80A8);
REX_HOOK_RAW(sub_826E80A8) { Count(46, ctx); __imp__sub_826E80A8(ctx, base); }
REX_EXTERN(__imp__sub_826E8928);
REX_HOOK_RAW(sub_826E8928) { Count(47, ctx); __imp__sub_826E8928(ctx, base); }
REX_EXTERN(__imp__sub_826E8C80);
REX_HOOK_RAW(sub_826E8C80) { Count(48, ctx); __imp__sub_826E8C80(ctx, base); }
REX_EXTERN(__imp__sub_826E8D60);
REX_HOOK_RAW(sub_826E8D60) { Count(49, ctx); __imp__sub_826E8D60(ctx, base); }
REX_EXTERN(__imp__sub_826E8E30);
REX_HOOK_RAW(sub_826E8E30) { Count(50, ctx); __imp__sub_826E8E30(ctx, base); }
REX_EXTERN(__imp__sub_826E8FD8);
REX_HOOK_RAW(sub_826E8FD8) { Count(51, ctx); __imp__sub_826E8FD8(ctx, base); }
REX_EXTERN(__imp__sub_826E92A0);
REX_HOOK_RAW(sub_826E92A0) { Count(52, ctx); __imp__sub_826E92A0(ctx, base); }
REX_EXTERN(__imp__sub_826E9CF0);
REX_HOOK_RAW(sub_826E9CF0) { Count(53, ctx); __imp__sub_826E9CF0(ctx, base); }
REX_EXTERN(__imp__sub_826EBBC0);
REX_HOOK_RAW(sub_826EBBC0) { Count(54, ctx); __imp__sub_826EBBC0(ctx, base); }
REX_EXTERN(__imp__sub_826EBCB0);
REX_HOOK_RAW(sub_826EBCB0) { Count(55, ctx); __imp__sub_826EBCB0(ctx, base); }
REX_EXTERN(__imp__sub_826EC9C0);
REX_HOOK_RAW(sub_826EC9C0) { Count(56, ctx); __imp__sub_826EC9C0(ctx, base); }
REX_EXTERN(__imp__sub_826ECF30);
REX_HOOK_RAW(sub_826ECF30) { Count(57, ctx); __imp__sub_826ECF30(ctx, base); }
REX_EXTERN(__imp__sub_826ED400);
REX_HOOK_RAW(sub_826ED400) { Count(58, ctx); __imp__sub_826ED400(ctx, base); }
REX_EXTERN(__imp__sub_826EE8E8);
REX_HOOK_RAW(sub_826EE8E8) { Count(59, ctx); __imp__sub_826EE8E8(ctx, base); }
REX_EXTERN(__imp__sub_826EE940);
REX_HOOK_RAW(sub_826EE940) { Count(60, ctx); __imp__sub_826EE940(ctx, base); }
REX_EXTERN(__imp__sub_826EEBC0);
REX_HOOK_RAW(sub_826EEBC0) { Count(61, ctx); __imp__sub_826EEBC0(ctx, base); }
REX_EXTERN(__imp__sub_826F1390);
REX_HOOK_RAW(sub_826F1390) { Count(62, ctx); __imp__sub_826F1390(ctx, base); }
REX_EXTERN(__imp__sub_826F3228);
REX_HOOK_RAW(sub_826F3228) { Count(63, ctx); __imp__sub_826F3228(ctx, base); }
REX_EXTERN(__imp__sub_826F3660);
REX_HOOK_RAW(sub_826F3660) { Count(64, ctx); __imp__sub_826F3660(ctx, base); }
REX_EXTERN(__imp__sub_826F36C8);
REX_HOOK_RAW(sub_826F36C8) { Count(65, ctx); __imp__sub_826F36C8(ctx, base); }
REX_EXTERN(__imp__sub_826F3858);
REX_HOOK_RAW(sub_826F3858) { Count(66, ctx); __imp__sub_826F3858(ctx, base); }
REX_EXTERN(__imp__sub_826F64A8);
REX_HOOK_RAW(sub_826F64A8) { Count(67, ctx); __imp__sub_826F64A8(ctx, base); }
REX_EXTERN(__imp__sub_826F7FB0);
REX_HOOK_RAW(sub_826F7FB0) { Count(68, ctx); __imp__sub_826F7FB0(ctx, base); }
