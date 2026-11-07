#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Parses a text command received on the MQTT command topic:
 *
 *   START        -> PROTOCOL_CMD_START
 *   STOP         -> PROTOCOL_CMD_STOP
 *   DUTY <0-100> -> PROTOCOL_CMD_SET_DUTY
 *
 * Case-insensitive; leading/trailing whitespace (e.g. a newline added by the
 * publishing tool) is ignored. Returns false for anything else.
 *
 * No ESP-IDF dependency, so it is unit tested on a PC (see tests/).
 */
bool CommandParser_Parse(const char *text, ProtocolCommand_t *command, uint8_t *value);

#endif /* COMMAND_PARSER_H */
