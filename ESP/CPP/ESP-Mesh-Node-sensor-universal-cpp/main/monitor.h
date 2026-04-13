#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void monitor_start(uint16_t target_addr);
void monitor_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_H */
