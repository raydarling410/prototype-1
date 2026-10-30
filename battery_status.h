#ifndef BATTERY_STATUS_H
#define BATTERY_STATUS_H

#include <stdint.h>
#include <stdbool.h>
#include "parameters.h"

//what the portable charger is doing right at this moment

typedef enum { 
    MODE_IDLE, MODE_CHARGING, MODE_DISCHARGING, MODE_PASSTHROUGH, MODE_COOLDOWN, MODE_FAULT
    /*Idle nothing plugged in, charging input plugged in pack is charging the device, Discharge is when the device is plugged in and the pack charges the device 
    passthrough is when both input charges pack and it powers the device, the cooldown is when the battery overheats and everything is off for it to cool
    the fault is when it needs clear */
} PackMode;

//LI ion charge algorithm stages 

typedef enum { 
    CHG_OFF, CHG_PRECHARGE, CHG_CC, CHG_CV, CHG_DONE
    /*prechrge is deeply drained and a small trickle current is needed 
    CC constant current 
    CV constant voltage, current tapers 
    Done is full waiting for voltage to drop to recharge */
} ChargePhase;

// Fault bits
#define FAULT_CELL_OV  (1u << 0)  // Cell over-voltage
#define FAULT_CELL_UV  (1u << 1)  // Cell under-voltage
#define FAULT_EMPTY    (1u << 2)  // Battery empty
#define FAULT_OC_CHG   (1u << 3)  // Over-current charge (latched)
#define FAULT_OC_DSG   (1u << 4)  // Over-current discharge (latched)
#define FAULT_SHORT    (1u << 5)  // Short circuit (latched)
#define FAULT_OVER_TEMP (1u << 6) // Over-temperature (cooldown)
#define FAULT_UT_CHG   (1u << 7)  // Too cold to charge
#define FAULT_UT_DSG   (1u << 8)  // Too cold to discharge
#define FAULT_SENSOR   (1u << 9)  // Sensor error (latched)
#define FAULT_BITS 10 

//these stay set until the user runs "clear" so it can reset
#define FAULTS_LATCHED (FAULT_OC_CHG | FAULT_OC_DSG | FAULT_SHORT | FAULT_SENSOR)

typedef struct { 
    //measurements will refresh every tick

    int32_t cell_mv[NUM_CELLS]; //cell voltages in mV
    int32_t pack_mv;            //pack voltage in mV
    int32_t current_ma;         //current in mA (positive = discharging, negative = charging)
    int32_t temp_dc;           //temperature in deci-Celsius (10x C)
    int32_t vmin_mv, vmax_mv;           //minimum cell voltage in mV and maximum
    int vmin_idx, vmax_idx;     //index of the minimum and maximum cell
    bool input_present;         //is the input connected
    bool load_present;          //is the load connected

    //state
    
    PackMode mode;             //current mode of operation
    ChargePhase phase;         //current charge phase
    uint32_t faults;            //bitfield of active faults
    int64_t charge_mams;        //cumulative charge in mA*ms (used for SOC estimation)
    //coulomb counter in mA*ms (used for SOC estimation)

    int32_t charge_target_ma;    //target charge current in mA (set by the charge algorithm)
    uint32_t balance_mask;         //bitfield of cells being balanced
    bool chg_fet, dsg_fet;          //state of the charge and discharge FETs
    bool cooling;                    //is the pack in cooldown mode
    uint32_t cooldown_start_ms;     //timestamp when cooldown started
    uint32_t cooldown_count;          //number of times the pack has entered cooldown
    uint32_t oc_timer_ms;             //timer for over-current detection
    uint32_t uptime_ms;                 //total uptime in milliseconds
    bool verbose;                        //verbose logging enabled
    //print events as they happen
}
BatteryPack;

void battery_init(BatteryPack *p);
void battery_update(BatteryPack *p, uint32_t dt_ms);
void battery_resync_soc(BatteryPack *p);
int battery_clear_faults(BatteryPack *p);
int32_t battery_soc_x10(const BatteryPack *p);//543 = 54.3%

const char *battery_mode_str(PackMode m);
const char *battery_phase_str(ChargePhase p);
const char *battery_fault_str(uint32_t bit);

void battery_print_status(const BatteryPack *p);
void battery_print_cells(const BatteryPack *p);
void battery_print_faults(const BatteryPack *p);
void battery_print_line(const BatteryPack *p);

#endif /*BATTERY_STATUS_H*/

