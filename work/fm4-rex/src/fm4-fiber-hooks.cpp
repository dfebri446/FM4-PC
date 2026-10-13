#include <rex/hook.h>

REX_EXTERN(rexcrt_ConvertThreadToFiber);
REX_EXTERN(rexcrt_ConvertFiberToThread);
REX_EXTERN(rexcrt_CreateFiber);
REX_EXTERN(rexcrt_DeleteFiber);
REX_EXTERN(rexcrt_SwitchToFiber);

// XAPI functions identified from the original PPC: 0xA50-byte contexts,
// KTHREAD.fiber_ptr at 0x164, kernel stack allocation/deletion, and the
// save/restore sequence ending in KeSetCurrentStackPointers. Native fibers
// preserve the C++ call stack as well as the guest non-volatile registers.
REX_HOOK_RAW(sub_82C77AD8) { rexcrt_ConvertThreadToFiber(ctx, base); }
REX_HOOK_RAW(sub_82C77B68) { rexcrt_ConvertFiberToThread(ctx, base); }
REX_HOOK_RAW(sub_82C77BB8) { rexcrt_CreateFiber(ctx, base); }
REX_HOOK_RAW(sub_82C77C98) { rexcrt_DeleteFiber(ctx, base); }
REX_HOOK_RAW(sub_82C7A460) { rexcrt_SwitchToFiber(ctx, base); }
