/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace rex::kernel::xboxkrnl {

// Optional asynchronous I/O trace (REX_IO_TRACE=<file>), one line per event:
// "<steady ms> <guest thread id> <event> <details>", where the details of
// NtReadFile include the guest return addresses of the four innermost frames.
bool IoTraceEnabled();
void IoTrace(std::string_view event, std::string_view details);
// Whether NtReadFile has used the handle as its completion event in the last
// two seconds.
bool IoTraceIsReadEvent(uint32_t handle);
// The guest return addresses of the current call chain, as hexadecimal text.
std::string IoTraceCallers();

}  // namespace rex::kernel::xboxkrnl
