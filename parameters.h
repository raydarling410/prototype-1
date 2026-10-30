#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <stdint.h>

#define NUM_CELLS 4 //li ion cells stacked in series
#define TICK_MS 100 //controls loop period
#define OC_DELAY_MS 500 //over current mus last this long to trip
#define CV_STEP_MA 20 //cv loop current a djustment per tick 
#define TEMP_HYST_DC 30 //3.0 C hysterisis for the under temperature release

/*runtime tunable parameters changes them with the "set command"
Units used everywhere in this project intergers no floats in firmware 
voltage mV
current mA (+ = flowing INTO the pack, - = flowing OUT of the pack)
temperature deci-degrees C (s50 = 25.0 C time ms)
*/

typedef struct { 
    int32_t capacity_mah; // rated cell capacity
    int32_t cell_v_max_mv; // cv setpoint at 100% full battery 
    int32_t cell_ovp_mv; // over voltage fault trip
    int32_t cell_ovp_release_mv; // over voltage release
    int32_t cell_v_min_mv; // soft cutoff "empty"
    int32_t cell_uvp_mv; // under voltage fault trip
    int32_t cell_uvp_release_mv; // under voltage release
    int32_t precharge_mv; // trickle charge below this voltage
    int32_t recharge_drop_mv; // restart charge if voltage drops this much

    int32_t charge_ma; // constant current charge current
    int32_t precharge_ma; // trickle rate for deeply drained cells
    int32_t term_ma; // termination current for CV phase (c/20)
    int32_t max_charge_ma; // over current trip for charge]
    int32_t max_discharge_ma; // over current trip for discharge
    int32_t short_circuit_ma; // short circuit trip

    int32_t temp_chg_min_dc; // no charging below this 
    int32_t temp_chg_max_dc; // cooldown if charging above this 
    int32_t temp_dsg_min_dc; // no discharging below this
    int32_t temp_dsg_max_dc; // cooldown if discharging above this
    int32_t temp_resume_dc; // coodown ends at or below this 
    int32_t cooldown_min_ms; // cooldown lasts at least this long

    int32_t balance_delta_mv; // balance cells this far above the lowest 
    int32_t balance_min_mv; // only balance cells above this voltage

}

BatteryParams;
extern BatteryParams g_params;

//describes one parameter so commands can list set it by name

typedef struct{
    const char *name;
    int32_t *value;
    int32_t min;
    int32_t max;
    const char *unit;
    const char *desc;

} ParamInfo;

//returns code for params_set
#define PARAM_OK 0
#define PARAM_UNKNOWN -1
#define PARAM_OUT_OF_RANGE -2
#define PARAM_INCONSISTENT -3

void params_load_default(void);
const ParamInfo *params_find(const char *name);
int params_set(const char *name, int32_t value);
void params_print(void);

#endif //PARAMETERS_H