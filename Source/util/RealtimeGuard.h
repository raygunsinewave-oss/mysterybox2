// Mystery Box Synthesizer by Raygun
// Debug-mode real-time safety tracker (spec §10): while processBlock runs, any
// heap allocation made on that thread is counted and triggers a jassert.
// Enabled when MBX_RT_ALLOC_CHECK=1 (on by default in Debug builds), compiled
// out entirely in Release builds.
#pragma once

#include <atomic>

#ifndef MBX_RT_ALLOC_CHECK
 #if defined(NDEBUG)
  #define MBX_RT_ALLOC_CHECK 0
 #else
  #define MBX_RT_ALLOC_CHECK 1
 #endif
#endif

namespace mbx::rt
{
/** Number of allocations detected on the audio thread since startup. */
std::atomic<int>& violationCount() noexcept;
/** True while the current thread is inside an audio callback. */
bool& inAudioCallback() noexcept;

struct ScopedAudioThread
{
    ScopedAudioThread() noexcept : prev (inAudioCallback()) { inAudioCallback() = true; }
    ~ScopedAudioThread() noexcept { inAudioCallback() = prev; }
    bool prev;
};

/** Temporarily allow allocation (e.g. for code paths known to be host-owned). */
struct ScopedAllowAllocation
{
    ScopedAllowAllocation() noexcept : prev (inAudioCallback()) { inAudioCallback() = false; }
    ~ScopedAllowAllocation() noexcept { inAudioCallback() = prev; }
    bool prev;
};
} // namespace mbx::rt

#if MBX_RT_ALLOC_CHECK
 #define MBX_REALTIME_SCOPE ::mbx::rt::ScopedAudioThread mbxRealtimeScope_
#else
 #define MBX_REALTIME_SCOPE
#endif
