#ifndef COMMANDS_H
#define COMMANDS_H

#include "battery_status.h"

#define CMD_OK 0
#define CMD_ERROR 1
#define CMD_QUIT 2

//parse and run one line of user input
int commands_execute(BatteryPack *p, char *line);

/*one full loop itereation (hardware step and firmware update) is implemented in main so the 
command  layer doesn't depend on the sim*/

void system_tick(BatteryPack *p);

#endif //COMMANDS_H