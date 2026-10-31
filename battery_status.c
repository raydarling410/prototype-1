#include "battery_status.h"
#include "Hal.h"
#include <stdio.h>
#include <string.h>

//helpers

/*open circuit voltage -> SOC table that is used to estimate the SOC at power up 
it will replace with the curve from the cell's datasheet */

static const int32_t OCV_TABLE[][2] = {
    //{mv, percent}
    {3000,0}, {3450, 10}, {3550, 20}, {3650, 40}, {3800, 60}, {3950, 80}, {4200, 100}
};

#define OCV_N ((int) (sizeof OCV_TABLE / sizeof OCV_TABLE[0]))

static int32_t ocv_to_soc_pct(int32_t mv) {
    if(mv <= OCV_TABLE[0][0]) return 0;
    for(int i = 1; i < OCV_N; i++) {
        if (mv <= OCV_TABLE[i][0]) {
            int32_t v0 = OCV_TABLE[i-1][0], v1 = OCV_TABLE[i][0];
            int32_t s0 = OCV_TABLE[i-1][1], s1 = OCV_TABLE[i][1];
            return s0 + (mv - v0) * (s1 - s0) / (v1 - v0);
        }
    }
    return 100;
}
static int64_t capacity_mams(void) { 
    return (int64_t)g_params.capacity_mah * 3600LL * 1000LL; 
    //mAh -> mA*ms
}

static void set_soc_pct(BatteryPack *p, int32_t pct) {
    p->charge_mams = capacity_mams() * pct / 100;
}

static void print_v(int32_t mv) { 
    int32_t a = mv < 0 ? -mv : mv;
    printf("%s%ld.%03ld V", mv < 0 ? "-" : "", (long)(a / 1000), (long)(a % 1000));
}

static void print_temp(int32_t dc) { 
    int32_t a = dc < 0 ? -dc : dc;
    printf("%s%ld.%01ld C", dc < 0 ? "-" : "", (long)(a / 10), (long)(a % 10));
}

static void print_time(uint32_t ms)
{
    printf("%lu.%lu s", (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
}

// measurments

static void measure(BatteryPack *p) {
    p->pack_mv = 0;
    p->vmin_mv = INT32_MAX;
    p->vmax_mv = INT32_MIN;
    for (int c = 0; c < NUM_CELLS; c++) {
        int32_t v = hal_read_cell_mv(c);
        p->cell_mv[c] = v;
        p->pack_mv += v;      /* series stack: voltages add */
        if (v < p->vmin_mv) { p->vmin_mv = v; p->vmin_idx = c; }
        if (v > p->vmax_mv) { p->vmax_mv = v; p->vmax_idx = c; }
    }
    p->current_ma    = hal_read_current_ma();
    p->temp_dc       = hal_read_temp_dc();
    p->input_present = hal_input_present();
    p->load_present  = hal_load_present(); 
}
//state of change (coulomb counting)

static void coulomb_count(BatteryPack *p,uint32_t dt_ms) { 
    p -> charge_mams += (int64_t)p->current_ma * dt_ms;
    if (p -> charge_mams < 0) p -> charge_mams = 0;
    if (p -> charge_mams > capacity_mams()) p -> charge_mams = capacity_mams();
}

//protection 
static void check_protection(BatteryPack *p, uint32_t dt_ms) { 
    const BatteryParams *g = &g_params;
    //sensor sanity: impossible readings means there is a broken wire/thermistor

    if ( p -> temp_dc < -400 || p -> temp_dc > 1250) 
    p->faults |= FAULT_SENSOR;
    for (int c = 0; c < NUM_CELLS; c++)
        if (p -> cell_mv[c] < 500 || p -> cell_mv[c] > 5000) p->faults |= FAULT_SENSOR;
    
    //over voltage any cell with hysteresis

    if (p->vmax_mv >= g->cell_ovp_mv) p->faults |= FAULT_CELL_OV;
    else if (p->vmax_mv <= g->cell_ovp_release_mv) p->faults &= ~FAULT_CELL_OV;
    if (p->vmin_mv <= g->cell_uvp_mv) p->faults |= FAULT_CELL_UV;
    else if (p->vmin_mv >= g->cell_uvp_release_mv) p->faults &= ~FAULT_CELL_UV;

    //soft "empty" cutoff stop discrarging before reaching the hard limit 
     if (!p->input_present && p->vmin_mv <= g->cell_v_min_mv) {
        if (!(p->faults & FAULT_EMPTY)) set_soc_pct(p, 0);   /* recalibrate SOC */
        p->faults |= FAULT_EMPTY;
    } else if (p->vmin_mv >= g->cell_uvp_release_mv) {
        p->faults &= ~FAULT_EMPTY;
    }
    /*Short circuit: instant trip*/ 
    if(-p->current_ma >= g->short_circuit_ma) p-> faults |= FAULT_SHORT;
    //over current: must persist OC_DELAY_MS (ignores brief spikes)
    bool oc_chg = p->current_ma > g->max_charge_ma; 
    bool oc_dsg = -p -> current_ma > g -> max_discharge_ma;
        if(oc_chg || oc_dsg) {
            p ->oc_timer_ms += dt_ms;
            if(p->oc_timer_ms >= OC_DELAY_MS) p -> faults |= oc_chg? FAULT_OC_CHG:FAULT_OC_DSG;
        } else {
            p -> oc_timer_ms = 0;
        }
        //over temp -> enter the cooldown (self disabling protection)
        int32_t t_max = p->input_present ? g->temp_chg_max_dc : g->temp_dsg_max_dc;
        if(!p->cooling && p -> temp_dc >= t_max){
            p->cooling = true;
            p->cooldown_start_ms = p ->uptime_ms;
            p->cooldown_count++;
            p->faults |= FAULT_OVER_TEMP;
        }

        //under temperature charging the LI-ion below 0c plates lithium gets permanent damage
        if (p -> temp_dc < g -> temp_chg_min_dc) p -> faults |= FAULT_UT_CHG;
        else if ( p -> temp_dc >= g-> temp_chg_min_dc + TEMP_HYST_DC) p -> faults &= ~FAULT_UT_CHG;

       if (p->temp_dc < g->temp_dsg_min_dc)p->faults |= FAULT_UT_DSG;
    else if (p->temp_dc >= g->temp_dsg_min_dc + TEMP_HYST_DC) p->faults &= ~FAULT_UT_DSG;
}

//Cooldown ends only when we have both cooled to resume temp and the minimum time passed 
static void update_cooldown(BatteryPack *p) {
    if(!p->cooling) return;
    uint32_t elapsed = p -> uptime_ms - p -> cooldown_start_ms;
    if(p -> temp_dc <= g_params.temp_resume_dc && elapsed >= (uint32_t) g_params.cooldown_min_ms){
        p->cooling = false;
        p->faults &= ~FAULT_OVER_TEMP;
    }
}

//Direction detecion -> mode
static void decide_mode(BatteryPack *p){
    if (p -> faults & FAULTS_LATCHED)
    p->mode = MODE_FAULT;
    else if (p -> cooling)
    p->mode = MODE_COOLDOWN;
    else if (p -> input_present && p -> load_present)
    p->mode = MODE_PASSTHROUGH;
    else if (p->input_present)
    p->mode = MODE_CHARGING;
    else if (p->load_present)
    p->mode = MODE_DISCHARGING;
    else
    p->mode = MODE_IDLE;

        if(!p->input_present) p ->phase = CHG_OFF;
        //unplugged will reset algorithm next time
}

//LI ion charge algorithm (percharge -> CC -> CV -> done)
static void charge_control(BatteryPack *p) {
    const BatteryParams *g = &g_params;
    bool allowed = (p->mode == MODE_CHARGING || p->mode == MODE_PASSTHROUGH) && !(p->faults & (FAULT_CELL_OV | FAULT_UT_CHG));    
    
    if(!allowed) { 
        p -> charge_target_ma = 0;
        if ( p -> mode == MODE_FAULT || p->mode == MODE_COOLDOWN) p ->phase = CHG_OFF;
        return;
    }
   /* Phase transitions */
    if (p->phase == CHG_OFF)
        p->phase = (p->vmin_mv < g->precharge_mv) ? CHG_PRECHARGE : CHG_CC;
    if (p->phase == CHG_PRECHARGE && p->vmin_mv >= g->precharge_mv)
        p->phase = CHG_CC;
    if (p->phase == CHG_CC && p->vmax_mv >= g->cell_v_max_mv) {
        p->phase = CHG_CV;
        p->charge_target_ma = g->charge_ma;
    }

//current target for this phase
switch(p->phase){ 
    case CHG_PRECHARGE: p->charge_target_ma = g->precharge_ma; 
        break;
    case CHG_CC: p->charge_target_ma = g->charge_ma; 
        break;
        case CHG_CV:
        //holds the highest cell at v_max by trimming current 
        if (p->vmax_mv > g->cell_v_max_mv) p->charge_target_ma -= CV_STEP_MA;
        else if (p->vmax_mv < g->cell_v_max_mv - 10) 
        p->charge_target_ma += CV_STEP_MA;
        if(p->charge_target_ma > g ->charge_ma) p->charge_target_ma = g->charge_ma;
        if(p->charge_target_ma <= g->term_ma){
            p->phase = CHG_DONE;
            p->charge_target_ma = 0;
            set_soc_pct(p, 100);

            // recalibrate SOC
        }

        break;
        case CHG_DONE: p->charge_target_ma = 0; 
        if(p->vmax_mv < g->cell_v_max_mv - g->recharge_drop_mv) p->phase = CHG_CC;
        break;
        default:
        p->charge_target_ma = 0;
        break;
    }
}

/*Passive cell balancing (bleed high cells while charging) In a series stack the same current 
that will flow through every cell so if there is any mismatch it grows over time unless the high cells are bled down*/

static void balance_control(BatteryPack *p) {
    p->balance_mask = 0;
    bool charging = (p -> phase == CHG_CC || p -> phase == CHG_CV);
        if (!charging || p-> mode == MODE_FAULT || p->mode == MODE_COOLDOWN) 
        return;

        for (int c = 0; c< NUM_CELLS; c++)
            if(p->cell_mv[c] - p ->vmin_mv > g_params.balance_delta_mv && p -> cell_mv[c] > g_params.balance_min_mv) 
            p -> balance_mask |= (1u << c);
}

//drive output 
static void apply_outputs(BatteryPack *p)
{
    p->chg_fet = (p->mode == MODE_CHARGING || p->mode == MODE_PASSTHROUGH) &&
                 p->charge_target_ma > 0 &&
                 !(p->faults & (FAULT_CELL_OV | FAULT_UT_CHG));
 
    p->dsg_fet = (p->mode == MODE_DISCHARGING || p->mode == MODE_PASSTHROUGH) &&
                 !(p->faults & (FAULT_CELL_UV | FAULT_EMPTY | FAULT_UT_DSG));
 
    hal_set_charge_current_ma(p->charge_target_ma);
    hal_set_charge_fet(p->chg_fet);
    hal_set_discharge_fet(p->dsg_fet);
    hal_set_balance_mask(p->balance_mask);
}

//event log 

static void log_events(const BatteryPack *p, PackMode old_mode, ChargePhase old_phase, uint32_t old_faults){
    if (!p->verbose) return;
    uint32_t set_bits = p->faults & ~old_faults;
    uint32_t clr_bits = old_faults & ~p->faults;
 
    for (int b = 0; b < FAULT_BITS; b++) {
        uint32_t bit = 1u << b;
        if (set_bits & bit) { printf("  [t="); print_time(p->uptime_ms); printf("] FAULT SET:   %s\n", battery_fault_str(bit)); }
        if (clr_bits & bit) { printf("  [t="); print_time(p->uptime_ms); printf("] fault clear: %s\n", battery_fault_str(bit)); }
    }
    if (p->mode != old_mode) {
        printf("  [t="); print_time(p->uptime_ms);
        printf("] mode %s -> %s\n", battery_mode_str(old_mode), battery_mode_str(p->mode));
    }
    if (p->phase != old_phase) {
        printf("  [t="); print_time(p->uptime_ms);
        printf("] charge %s -> %s\n", battery_phase_str(old_phase), battery_phase_str(p->phase));
    }
}

//public API 
void battery_init(BatteryPack *p)
{ 
    memset(p, 0, sizeof(*p));
    p->verbose = true;
    p->mode = MODE_IDLE;
    p->phase = CHG_OFF;
    battery_resync_soc(p);
    apply_outputs(p);
}

//estimate SOC from resting cell voltage will only be accurate with -0 current

void battery_resync_soc(BatteryPack *p) {
    measure(p);
    int32_t sum = 0;
    for(int c = 0; c < NUM_CELLS; c++)
    sum += p->cell_mv[c];
    set_soc_pct(p, ocv_to_soc_pct(sum / NUM_CELLS));
}

//one control loop iteration call every tick_ms milliseconds
void battery_update(BatteryPack *p, uint32_t dt_ms) {
    PackMode old_mode = p->mode;
    ChargePhase old_phase = p->phase;
    uint32_t old_faults = p->faults;

    p->uptime_ms += dt_ms;
    measure(p);
    coulomb_count(p, dt_ms);
    check_protection(p, dt_ms);
    update_cooldown(p);
    decide_mode(p);
    charge_control(p);
    balance_control(p);
    apply_outputs(p);

    log_events(p, old_mode, old_phase, old_faults);
}

int battery_clear_faults(BatteryPack *p)
{
    int n = 0;
    for (int b = 0; b < FAULT_BITS; b++) {
        if (p->faults & FAULTS_LATCHED & (1u << b))
            n++;
    }
    p->faults &= ~FAULTS_LATCHED;
    p->oc_timer_ms = 0;
    return n;
}

int32_t battery_soc_x10(const BatteryPack *p){
    return (int32_t)(p->charge_mams * 1000 / capacity_mams());
}

const char *battery_mode_str(PackMode m){
    switch(m){
        case MODE_IDLE: return "IDLE";
        case MODE_CHARGING: return "CHARGING";
        case MODE_DISCHARGING: return "DISCHARGING";
        case MODE_PASSTHROUGH: return "PASSTHROUGH";
        case MODE_COOLDOWN: return "COOLDOWN";
        case MODE_FAULT: return "FAULT";
    }
    return "? UNKOWN";
}

const char *battery_phase_str(ChargePhase ph){
    switch(ph){
        case CHG_OFF: return "OFF";
        case CHG_PRECHARGE: return "PRECHARGE";
        case CHG_CC: return "CC";
        case CHG_CV: return "CV";
        case CHG_DONE: return "DONE";
    }
    return "? UNKOWN";
}

const char *battery_fault_str(uint32_t bit)
{
    switch (bit) {
    case FAULT_CELL_OV:   return "CELL_OVER_VOLTAGE";
    case FAULT_CELL_UV:   return "CELL_UNDER_VOLTAGE";
    case FAULT_EMPTY:     return "BATTERY_EMPTY";
    case FAULT_OC_CHG:    return "OVER_CURRENT_CHARGE (latched)";
    case FAULT_OC_DSG:    return "OVER_CURRENT_DISCHARGE (latched)";
    case FAULT_SHORT:     return "SHORT_CIRCUIT (latched)";
    case FAULT_OVER_TEMP: return "OVER_TEMPERATURE (cooldown)";
    case FAULT_UT_CHG:    return "TOO_COLD_TO_CHARGE";
    case FAULT_UT_DSG:    return "TOO_COLD_TO_DISCHARGE";
    case FAULT_SENSOR:    return "SENSOR_ERROR (latched)";
    }
    return "?";
}

static const char *direction_str(const BatteryPack *p)
{
    switch (p->mode) {
    case MODE_CHARGING:    return "input connected -> portable is being charged";
    case MODE_DISCHARGING: return "device connected -> portable is charging the device";
    case MODE_PASSTHROUGH: return "input + device -> charging pack and powering device";
    case MODE_COOLDOWN:    return "all outputs off, waiting to cool";
    case MODE_FAULT:       return "all outputs off, run 'faults' then 'clear'";
    default:               return "nothing connected";
    }
}

//check the measured current agrees with the detected direction 

static const char *direction_check(const BatteryPack *p) { 
    if(p->mode == MODE_CHARGING && p->chg_fet && p ->current_ma < -50) return "WARNING: current is flowing out while charging";
    if (p->mode == MODE_DISCHARGING && p->current_ma > 50) 
    return "WARNING: current is flowing in while discharging";
    return "";
}

void battery_print_status(const BatteryPack *p) {
    int32_t soc = battery_soc_x10(p);
    int64_t mw = (int64_t) p->pack_mv * p -> current_ma /1000;

    printf(" ==== Pack status(t=");
    print_time(p->uptime_ms);
    printf(") ====\n");
    printf(" Mode : %s", battery_mode_str(p->mode));

    if(p->phase != CHG_OFF) printf(" (%s)", battery_phase_str(p->phase));
    printf("\n Direction: %s%s\n", direction_str(p), direction_check(p));
    printf(" Pack :"); 
    print_v(p->pack_mv);

    printf(" %+ld mA %+lld mW\n", (long)p->current_ma, (long long)mw);
    printf(" SOC: %ld.%ld%%\n",(long) (soc / 10),(long) (soc % 10));
    printf("Temperature : ");
    print_temp(p -> temp_dc);
    if(p->cooling){
        printf(" (cooling, resume at ");
        print_temp(g_params.temp_resume_dc);
        printf(")");
    
    }
    printf("\n Cells : min");
    print_v(p->vmin_mv);
    printf(" (#%d) max ", p->vmin_idx + 1);
    print_v(p->vmax_mv);
    printf("(#%d) delta %ld mV\n" , p ->vmax_idx + 1, (long) (p->vmax_mv - p->vmin_mv));
    printf(" Output : CHG FET %s DSG FET %s charge target %ld mA balance 0x%lX\n", p ->chg_fet ? "ON" : "off", p->dsg_fet ? "ON" : "off", (long)p->charge_target_ma, (unsigned long)p->balance_mask);
    printf(" Faults : %s\n", p->faults ? "see 'faults'" : "none");
    }

    void battery_print_cells(const BatteryPack *p) {
    for(int c = 0; c < NUM_CELLS; c++) { 
            printf(" Cell %d : ", c + 1);
            print_v(p->cell_mv[c]);
            printf(" %+5ld mV vs min", (long) (p->cell_mv[c] - p->vmin_mv));
       if (p->balance_mask & (1u << c)) printf("  [BALANCING]");
        if (p->cell_mv[c] >= g_params.cell_ovp_mv) printf("  [OV]");
        if (p->cell_mv[c] <= g_params.cell_uvp_mv) printf("  [UV]");
        printf("\n");
    }
    printf("  Stack  : "); print_v(p->pack_mv); printf("  (%d cells in series)\n", NUM_CELLS);
}

void battery_print_line(const BatteryPack *p)
{
    int32_t soc = battery_soc_x10(p);
    printf("  t="); print_time(p->uptime_ms);
    printf("  %-11s %-9s ", battery_mode_str(p->mode), battery_phase_str(p->phase));
    print_v(p->pack_mv);
    printf("  %+6ld mA  SOC %3ld.%ld%%  ", (long)p->current_ma, (long)(soc / 10), (long)(soc % 10));
    print_temp(p->temp_dc);
    printf("  faults 0x%03lX\n", (unsigned long)p->faults);
}

void battery_print_faults(const BatteryPack *p)
{
    if (!p->faults) printf("  No active faults.\n");
    for (int b = 0; b < FAULT_BITS; b++)
        if (p->faults & (1u << b)) printf("  - %s\n", battery_fault_str(1u << b));
    printf("  Cooldown events since start: %lu\n", (unsigned long)p->cooldown_count);
}