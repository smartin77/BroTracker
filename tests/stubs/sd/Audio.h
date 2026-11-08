#pragma once
// Host-only platform boundary for the actual SD producer, not an audio graph.
#include <cstdint>
namespace FakeStreamPlatform {
inline std::uint32_t primask=0,ticks=0;
inline void (*on_barrier)()=nullptr;
}
inline std::uint32_t micros() { return FakeStreamPlatform::ticks++; }
inline void __DMB() {
    if(FakeStreamPlatform::on_barrier)FakeStreamPlatform::on_barrier();
}
