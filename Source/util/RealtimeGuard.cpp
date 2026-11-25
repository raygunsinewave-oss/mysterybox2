// Mystery Box Synthesizer by Raygun
#include "RealtimeGuard.h"
#include <cstdio>
#include <cstdlib>
#include <new>

namespace mbx::rt
{
std::atomic<int>& violationCount() noexcept
{
    static std::atomic<int> count { 0 };
    return count;
}

bool& inAudioCallback() noexcept
{
    thread_local bool flag = false;
    return flag;
}
} // namespace mbx::rt

#if MBX_RT_ALLOC_CHECK
// Replacement global allocation functions. Plugins are built with hidden symbol
// visibility, so these only affect this binary, never the host.
namespace
{
void reportViolation (std::size_t size) noexcept
{
    auto& flag = mbx::rt::inAudioCallback();
    flag = false; // avoid recursion while reporting
    const int n = ++mbx::rt::violationCount();
    if (n <= 8) std::fprintf (stderr, "[MBX] real-time violation: %zu-byte allocation on the audio thread\n", size);
   #if defined(MBX_RT_ALLOC_ASSERT) && MBX_RT_ALLOC_ASSERT
    std::abort();
   #endif
    flag = true;
}

void* checkedAlloc (std::size_t size)
{
    if (mbx::rt::inAudioCallback()) reportViolation (size);
    if (void* p = std::malloc (size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
} // namespace

void* operator new (std::size_t size) { return checkedAlloc (size); }
void* operator new[] (std::size_t size) { return checkedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept { if (mbx::rt::inAudioCallback()) reportViolation (size); return std::malloc (size == 0 ? 1 : size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { if (mbx::rt::inAudioCallback()) reportViolation (size); return std::malloc (size == 0 ? 1 : size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
#endif
