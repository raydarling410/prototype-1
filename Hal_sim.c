/*simulated hardware so the BMS can be tested on a pc 
This file is not firware: it only plays the role of the cells, charger, MOSFETs and thermistor. Floats are fine here */

#include "Hal.h"
#include "parameters.h"
#include <stdio.h>

#define SIM_CELL_R_MOHM 50.0 //internal resistance per cell 
#define SIM_BALANCE_MA 100.0 //bleed current through a balance rsistor 
#define SIM_HEAT_K 0.033 //deg C per second per A^2 (I^2 R heating)
#define SIM_COOL_K 0.1 //1/s cooling toward ambient
#define SIM_MAX_SOC 105.0 //allow overcharge so OVP can be tested

static struct {
    double charge_mas[NUM_CELLS]; // true charge in each cell mA*s
    double temp_c;
    double ambient_c;
    double current_ma;
    bool input;
    int32_t load_ma;
    bool chg_fet, dsg_fet;
    int32_t chg_cmd_ma;
    uint32_t bal_mask;
} sim;

// open circuit voltage vs state of charge 

static const double OCV[][2] = { 
   {0, 2700}, {5, 3300}, {10, 3450}, {20, 3550}, {40, 3650},
    {60, 3800}, {80, 3950}, {100, 4200}, {105, 4400}
};
#define OCV_N ((int) (sizeof OCV / sizeof OCV[0])) 

static double cap_mas(void) {
    return g_params.capacity_mah * 3600.0;
}
static  double soc_pct(int c) { 
    return 100.0 * sim.charge_mas[c] / cap_mas();
}
static double ocv_mv(double soc) { 
    if(soc <= OCV[0][0]) 
    return OCV[0][1];
    for (int i = 1; i < OCV_N; i++) {
        if(soc <= OCV[i][0]){
            double f = (soc - OCV[i - 1][0]) / (OCV[i][0] - OCV[i - 1][0]);
            return OCV[i-1][1]+f*(OCV[i][1] - OCV [i-1][1]);
        }
    }
    return OCV[OCV_N -1][1];

}

static int32_t round_i32(double x) { 
    return (int32_t) (x >= 0 ? x + 0.5 : x - 0.5);
}
void hal_init(void){
    for(int c = 0; c<NUM_CELLS; c++) sim.charge_mas[c] = 0.5 * cap_mas();
    sim.temp_c = sim.ambient_c = 25.0;
    sim.current_ma = 0;
    sim.input = false;
    sim.load_ma = 0;
    sim.chg_fet = sim.dsg_fet = false;
    sim.chg_cmd_ma = 0;
    sim.bal_mask = 0;
}

int32_t hal_read_cell_mv(int cell){
    double soc = soc_pct(cell);
    return round_i32(ocv_mv(soc) + sim.current_ma * SIM_CELL_R_MOHM / 1000.0);
}

int32_t hal_read_current_ma(void){
    return round_i32(sim.current_ma);
}
int32_t hal_read_temp_dc(void){
    return round_i32(sim.temp_c * 10.0);
}
bool hal_input_present(void) {
    return sim.input;
}
bool hal_load_present(void) { 
    return sim.load_ma > 0;
}
void hal_set_charge_fet(bool on) { 
    sim.chg_fet = on;
}
void hal_set_discharge_fet(bool on){
    sim.dsg_fet = on;
}
void hal_set_charge_current_ma(int32_t ma){
    sim.chg_cmd_ma = ma;
}
void hal_set_balance_mask(uint32_t mask) { 
    sim.bal_mask = mask;
}
//Advance the physucal model ny dt_ma using the current FET/charger output 
//currently unkown at the moment with no physical model tests

void hal_sim_step(uint32_t dt_ms){
    double dt = dt_ms / 1000.0;
    double i = 0.0;
    
    if(sim.input){
        //input supplies the load directly (pass-through): pack only will see charge current 

        if(sim.chg_fet) 
        i = sim.chg_cmd_ma;
    } else if (sim.dsg_fet){
        i = -(double)sim.load_ma;
    }
    sim.current_ma = i;

    for(int c = 0; c < NUM_CELLS; c++){
        double ic = i - (((sim.bal_mask >> c) & 1u) ? SIM_BALANCE_MA : 0.0);
        sim.charge_mas[c] += ic * dt;
        if(sim.charge_mas[c] < 0) sim.charge_mas[c] = 0;
        if(sim.charge_mas[c] > cap_mas() * SIM_MAX_SOC / 100.0)
        sim.charge_mas[c] = cap_mas() * SIM_MAX_SOC / 100.0;
    }

    double ia = i / 1000.0;
    sim.temp_c += (SIM_HEAT_K * ia * ia - SIM_COOL_K * (sim.temp_c - sim.ambient_c)) * dt;
}

void hal_sim_set_input(bool on) { 
    sim.input = on; 
}
void hal_sim_set_load_ma(int32_t ma) { 
    sim.load_ma = ma < 0 ? 0 : ma;
}
void hal_sim_set_temp_dc(int32_t dc) { 
    sim.temp_c = dc / 10.0;
}
void hal_sim_set_ambient_dc(int32_t dc) {
    sim.ambient_c = dc / 10.0;
}
void hal_sim_set_soc_pct(int cell, double pct) {
    if(pct < 0) pct = 0;
    if(pct > SIM_MAX_SOC) pct = SIM_MAX_SOC;
    for (int c = 0; c < NUM_CELLS; c++) 
    if (cell < 0 || cell == c) 
    sim.charge_mas[c] = cap_mas() * pct/100.0;
}

void hal_sim_print(void){
    printf(" [sim] input %s load: %ld mA temp: %.1f C ambient: %.1f C\n", sim.input ? "ON" : "off", (long)sim.load_ma, sim.temp_c, sim.ambient_c);
    printf(" [sim] true cell SOC:");
    for (int c = 0; c < NUM_CELLS; c++)
    printf("#%d %.1f%%", c + 1, soc_pct(c));
    printf("\n");
}

     