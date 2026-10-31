#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

/*Hardware abstract layer 
batterystatus.c only talks to the hardware through these functions 
hal.sim.c implements them with a software battery model so the whole progtam runs on a PC.
FOR A REAL BOARD, write hal_board.c with the same functions (ADC / battery-monitor chip reads GPIO fo MOSFETs charger IC current settings)
link that instead */

void hal_init(void);
int32_t hal_read_cell_mv(int cell);
//cell index 0 NUMCELLS-1
int32_t hal_read_current_ma(void);
//into pack - out
int32_t hal_read_temp_dc(void);
//deci-degrees C 
bool hal_input_present(void);
//wall/USB input device 
bool hal_load_present(void);
//device on output port 

void hal_set_charge_fet(bool on);
void hal_set_discharge_fet(bool on);
void hal_set_charge_current_ma(int32_t ma);
void hal_set_balance_mask(uint32_t mask);
//bit n = bleed cell n 

//simulation only controls (used by the 'sim' command)

void hal_sim_step(uint32_t dt_ms);
void hal_sim_set_input(bool on);
void hal_sim_set_load_ma(int32_t ma);
void hal_sim_set_temp_dc(int32_t dc);
void hal_sim_set_ambient_dc(int32_t dc);
void hal_sim_set_soc_pct(int cell, double pct);
//cell < 0 = all cells 
void hal_sim_print(void);

#endif