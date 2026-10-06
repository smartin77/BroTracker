#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Types: 1 init, 2 bus reset, 3 configuration, 4 TX alt, 5 RX alt,
// 6 audio configure, 7 TX state change, 8 update input/queue state change. Values are type-specific.
void brotracker_usb_lifecycle_event(uint32_t type,uint32_t a,uint32_t b,uint32_t c);
#ifdef __cplusplus
}
#endif
