#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdint.h>

void process_gatt_command(const char *cmd, uint16_t len);

#endif /* COMMAND_PARSER_H */
