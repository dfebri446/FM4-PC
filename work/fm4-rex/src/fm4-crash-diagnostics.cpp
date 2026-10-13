#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
LONG CALLBACK FM4CaptureNullAccess(EXCEPTION_POINTERS* exception) {
  // Runtime memory-watch faults are handled by its earlier VEH. Record only
  // unmapped low guest addresses, without recovering the fault.
  if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      exception->ExceptionRecord->NumberParameters < 2 ||
      exception->ExceptionRecord->ExceptionInformation[1] - 0x100000000ull >= 0x10000ull)
    return EXCEPTION_CONTINUE_SEARCH;
  const auto* context = exception->ContextRecord;
  FILE* output = nullptr;
  if (fopen_s(&output, "guest-null-crash.txt", "w") == 0 && output) {
    std::fprintf(output, "fault_address=%llx operation=%llx\n",
                 exception->ExceptionRecord->ExceptionInformation[1],
                 exception->ExceptionRecord->ExceptionInformation[0]);
    std::fprintf(output, "image_base=%p rip=%llx rsp=%llx rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx\n",
                 GetModuleHandleW(nullptr), context->Rip, context->Rsp, context->Rax,
                 context->Rbx, context->Rcx, context->Rdx, context->Rsi, context->Rdi);
    std::fprintf(output, "r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx\n",
                 context->R8, context->R9, context->R10, context->R11,
                 context->R12, context->R13, context->R14, context->R15);
    std::fclose(output);
  }
  HANDLE dump = CreateFileW(L"guest-null-crash.dmp", GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                     MiniDumpWithThreadInfo, &info, nullptr, nullptr);
    CloseHandle(dump);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
}

void FM4InstallCrashDiagnostics() {
  if (std::getenv("FM4_CAPTURE_DIR"))
    AddVectoredExceptionHandler(0, FM4CaptureNullAccess);
}
