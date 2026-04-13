#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void process_gatt_command(const char *cmd, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_PARSER_H */
