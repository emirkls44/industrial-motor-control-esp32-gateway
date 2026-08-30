#ifndef COMMAND_BRIDGE_H
#define COMMAND_BRIDGE_H

void CommandBridge_Init(void);
void CommandBridge_HandleMqttCommand(const char *command);

#endif
