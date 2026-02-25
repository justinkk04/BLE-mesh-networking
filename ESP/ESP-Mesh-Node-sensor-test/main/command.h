#ifndef COMMAND_H
#define COMMAND_H

#include <stddef.h>

// Format sensor data as "D:50%,V:12.003V,I:250.00mA,P:3000.8mW"
int format_sensor_response(char *buf, size_t buf_size);

// Process a text command (read, duty:50, r, s, etc.)
// Writes response to buf, returns response length.
int process_command(const char *cmd, char *response, size_t resp_size);

// FreeRTOS task: serial console for local testing via idf.py monitor
void console_task(void *pvParameters);

#endif // COMMAND_H
