/*Lithium Ion Battery distribution program
protoype not tested with any real batterys  */

#include <stdio.h>
#include "parameters.h"
#include "commands.h"
#include "battery_status.h"
#include "Hal.h"

void system_tick(BatteryPack * p) { 
    hal_sim_step(TICK_MS); //last tick of the hardware 
    battery_update(p, TICK_MS); //update the battery on its state
}


int main(void) { 
    
    BatteryPack pack;
    char line[128];

    params_load_default();
    hal_init();
    battery_init(&pack);


    printf("Lithium portable charger (%d cells in series)\n", NUM_CELLS);
    printf("Type 'help' for a list of commands you can use\n");

    for(;;){
        printf("bms> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        if (commands_execute(&pack, line) == CMD_QUIT) break;
    }

    //Safe shutdown and everything is off
    hal_set_charge_fet(false);
    hal_set_discharge_fet(false);
    hal_set_balance_mask(0);
    printf("\nOutputs are now disabled. Bye.\n");
    return 0;
}