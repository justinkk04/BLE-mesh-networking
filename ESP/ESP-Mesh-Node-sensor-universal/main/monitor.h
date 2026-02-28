#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

void monitor_start(uint16_t target_addr);
void monitor_stop(void);

#endif /* MONITOR_H */
