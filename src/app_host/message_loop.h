#pragma once

namespace nimblerun {

// NR-117: the message-loop dispatch decision. Win32 GetMessageW returns a
// positive value for a retrieved message, 0 for WM_QUIT, and -1 on failure;
// only a positive result carries a valid MSG to translate and dispatch.
// Isolated as an inline pure function so the three outcomes are unit-tested
// without driving the real Win32 message loop.
inline bool ShouldDispatchMessage(int get_message_result) {
    return get_message_result > 0;
}

} // namespace nimblerun
