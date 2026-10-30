#include "parameters.h"
#include <stdio.h>
#include <string.h>

BatteryParams g_params;

/* Defaults for a typical 18650 style LI ion cell (NMC chemistry) 
will always check these against the datasheet of the cells you use
*/

static const BatteryParams DEFAULTS = { 
   .capacity_mah        = 3000,
 
    .cell_v_max_mv       = 4200,
    .cell_ovp_mv         = 4250,
    .cell_ovp_release_mv = 4150,
    .cell_v_min_mv       = 3000,
    .cell_uvp_mv         = 2800,
    .cell_uvp_release_mv = 3100,
    .precharge_mv        = 3000,
    .recharge_drop_mv    = 100,
 
    .charge_ma           = 1500,   /* 0.5C */
    .precharge_ma        = 150,    /* 0.05C */
    .term_ma             = 150,    /* C/20 */
    .max_charge_ma       = 2000,
    .max_discharge_ma    = 3000,
    .short_circuit_ma    = 8000,
 
    .temp_chg_min_dc     = 0,      /*  0.0 C */
    .temp_chg_max_dc     = 450,    /* 45.0 C */
    .temp_dsg_min_dc     = -200,   /* -20.0 C */
    .temp_dsg_max_dc     = 600,    /* 60.0 C */
    .temp_resume_dc      = 400,    /* 40.0 C */
    .cooldown_min_ms     = 30000,  /* 30 s */
 
    .balance_delta_mv    = 30,
    .balance_min_mv      = 3900,
};

static const ParamInfo TABLE[] = {
    {"capacity",       &g_params.capacity_mah,        100,   50000, "mAh", "cell capacity"},
    {"v_max",          &g_params.cell_v_max_mv,       3600,   4400, "mV",  "CV setpoint / full"},
    {"ovp",            &g_params.cell_ovp_mv,         3700,   4500, "mV",  "over-voltage trip"},
    {"ovp_release",    &g_params.cell_ovp_release_mv, 3600,   4400, "mV",  "over-voltage release"},
    {"v_min",          &g_params.cell_v_min_mv,       2500,   3500, "mV",  "empty / soft cutoff"},
    {"uvp",            &g_params.cell_uvp_mv,         2000,   3300, "mV",  "under-voltage trip"},
    {"uvp_release",    &g_params.cell_uvp_release_mv, 2500,   3700, "mV",  "under-voltage release"},
    {"precharge_v",    &g_params.precharge_mv,        2000,   3500, "mV",  "trickle-charge below"},
    {"recharge_drop",  &g_params.recharge_drop_mv,      20,    500, "mV",  "restart-charge drop"},
    {"charge_i",       &g_params.charge_ma,             50,  10000, "mA",  "CC charge current"},
    {"precharge_i",    &g_params.precharge_ma,          10,   2000, "mA",  "trickle current"},
    {"term_i",         &g_params.term_ma,               10,   2000, "mA",  "CV termination current"},
    {"max_chg_i",      &g_params.max_charge_ma,        100,  20000, "mA",  "charge over-current trip"},
    {"max_dsg_i",      &g_params.max_discharge_ma,     100,  30000, "mA",  "discharge over-current trip"},
    {"short_i",        &g_params.short_circuit_ma,     500,  60000, "mA",  "short-circuit trip"},
    {"t_chg_min",      &g_params.temp_chg_min_dc,     -200,    200, "dC",  "min charge temp"},
    {"t_chg_max",      &g_params.temp_chg_max_dc,      300,    600, "dC",  "max charge temp"},
    {"t_dsg_min",      &g_params.temp_dsg_min_dc,     -400,    100, "dC",  "min discharge temp"},
    {"t_dsg_max",      &g_params.temp_dsg_max_dc,      300,    750, "dC",  "max discharge temp"},
    {"t_resume",       &g_params.temp_resume_dc,       200,    600, "dC",  "cooldown resume temp"},
    {"cooldown_ms",    &g_params.cooldown_min_ms,        0, 600000, "ms",  "minimum cooldown time"},
    {"bal_delta",      &g_params.balance_delta_mv,       5,    200, "mV",  "balance threshold"},
    {"bal_min",        &g_params.balance_min_mv,      3000,   4200, "mV",  "balance only above"},
};

#define TABLE_LEN ((int)(sizeof TABLE / sizeof TABLE[0]))

void params_load_default(void) {
    g_params = DEFAULTS;
}

const ParamInfo *params_find(const char *name) {
    for (int i = 0; i < TABLE_LEN; i++) 
        if (strcmp(TABLE[i].name, name) == 0)
        return &TABLE[i];
        return NULL;
}

//Cross check so the user can't set limits that contradict each other

static const char *param_check(void) {
    const BatteryParams *p = *g_oarams;
    if(p->cell_v_max_mv >= p ->cell_ovp_mv)
    return"v_max must be below the ovp";
    if (p->cell_ovp_release_mv >= p->cell_ovp_mv)    
    return "ovp_release must be below ovp";
    if (p->cell_uvp_mv >= p->cell_v_min_mv)          
    return "uvp must be below v_min";
    if (p->cell_uvp_release_mv <= p->cell_uvp_mv)    
    return "uvp_release must be above uvp";
    if (p->cell_v_min_mv >= p->cell_v_max_mv)        
    return "v_min must be below v_max";
    if (p->term_ma >= p->charge_ma)                  
    return "term_i must be below charge_i";
    if (p->precharge_ma > p->charge_ma)              
    return "precharge_i must not exceed charge_i";
    if (p->charge_ma >= p->max_charge_ma)            
    return "charge_i must be below max_chg_i";
    if (p->max_discharge_ma >= p->short_circuit_ma)  
    return "max_dsg_i must be below short_i";
    if (p->temp_chg_min_dc >= p->temp_chg_max_dc)    
    return "t_chg_min must be below t_chg_max";
    if (p->temp_resume_dc >= p->temp_chg_max_dc)     
    return "t_resume must be below t_chg_max";
    return NULL;
}

int params_set(const char *name, int32_t value) {
    const ParamInfo *info = params_find(name);
    if(!info) return PARAM_UNKNOWN;
    if(value < info->min || value > info->max)
    return PARAM_OUT_OF_RANGE;

    int32_t old_value = *info->value;
    *info->value = value;
    constc char *problem = param_check();
    if(problem) {
        *info -> value = old; //roll back

        printf(" rejected %s\n", problem);
        return PARAM_INCONSISTENT;
    }
    return PARAM_OK;
}

void params_print(void) {
   printf("  %-14s %8s  %-4s %s\n", "NAME", "VALUE", "UNIT", "DESCRIPTION");
    for (int i = 0; i < TABLE_LEN; i++)
        printf("  %-14s %8ld  %-4s %s  [%ld..%ld]\n",
               TABLE[i].name, (long)*TABLE[i].value, TABLE[i].unit,
               TABLE[i].desc, (long)TABLE[i].min, (long)TABLE[i].max);
    printf("  (dC = deci-degrees C, e.g. 450 = 45.0 C)\n");
}