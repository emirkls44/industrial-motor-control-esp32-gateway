#include "command_parser.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#define MAX_COMMAND_LENGTH 32U
#define MAX_DUTY_PERCENT   100U

/* Copies `text` into `out` without surrounding whitespace, upper-cased.
 * Returns false if it does not fit. */
static bool normalise(const char *text, char *out, size_t out_size)
{
    while (isspace((unsigned char)*text))
    {
        text++;
    }

    size_t length = strlen(text);

    while ((length > 0U) && isspace((unsigned char)text[length - 1U]))
    {
        length--;
    }

    if (length >= out_size)
    {
        return false;
    }

    for (size_t i = 0U; i < length; i++)
    {
        out[i] = (char)toupper((unsigned char)text[i]);
    }
    out[length] = '\0';

    return true;
}

/* Parses a decimal number 0..100 that makes up the whole string. */
static bool parse_duty(const char *text, uint8_t *duty)
{
    if (*text == '\0')
    {
        return false;
    }

    unsigned int number = 0U;

    for (const char *p = text; *p != '\0'; p++)
    {
        if (!isdigit((unsigned char)*p))
        {
            return false;
        }

        number = (number * 10U) + (unsigned int)(*p - '0');

        if (number > MAX_DUTY_PERCENT)
        {
            return false;
        }
    }

    *duty = (uint8_t)number;
    return true;
}

bool CommandParser_Parse(const char *text, ProtocolCommand_t *command, uint8_t *value)
{
    char normalised[MAX_COMMAND_LENGTH];

    if ((text == NULL) || (command == NULL) || (value == NULL) ||
        !normalise(text, normalised, sizeof(normalised)))
    {
        return false;
    }

    if (strcmp(normalised, "START") == 0)
    {
        *command = PROTOCOL_CMD_START;
        *value = 0U;
        return true;
    }

    if (strcmp(normalised, "STOP") == 0)
    {
        *command = PROTOCOL_CMD_STOP;
        *value = 0U;
        return true;
    }

    static const char duty_prefix[] = "DUTY";
    const size_t prefix_length = sizeof(duty_prefix) - 1U;

    if ((strncmp(normalised, duty_prefix, prefix_length) == 0) &&
        isspace((unsigned char)normalised[prefix_length]))
    {
        const char *argument = &normalised[prefix_length];

        while (isspace((unsigned char)*argument))
        {
            argument++;
        }

        uint8_t duty;

        if (parse_duty(argument, &duty))
        {
            *command = PROTOCOL_CMD_SET_DUTY;
            *value = duty;
            return true;
        }
    }

    return false;
}
