#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "commands.h"
#include "parameters.h"
#include "battery.h"


#define MAX_ARGS 8

typedef int (*CmdFn)(BatteryPack * p, int argc, char ** argv);

typedef struct {
    const char * name;
    const char * help;
    CmdFn fn;
} Command;



static bool parse_i32(const char*s, int32_t*out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0') 
    return false;
    *out = (int32_t)v;
    return true;
}

static bool parse_double(const char *s, double *out) { 
    char *end; 
    errno = 0;
    double v = strtod(s, &end);
    if (errno || end == s || *end != '\0')
    return false;
    *out = v; 
    return true;
}

static int32_t c_to_dc(double c) { 
    return (int32_t)(c >= 0 ? c * 10 + 0.5 : c * 10 - 0.5);
}


static int cmd_help(BatteryPack * p, int argc, char ** argv);

static int cmd_help(BatteryPack *p, int argc, char **argv);

static int cmd_status(BatteryPack *p, int argc, char **argv) { 
    (void)argc;
    (void)argv;
    battery_print_status(p);
    return CMD_OK;
}

static int cmd_cells(BatteryPack *p, int argc, char **argv) { 
    (void)argc;
    (void)argv;
    battery_print_cells(p);
    return CMD_OK;
}

static int cmd_faults (BatteryPack *p, int argc, char **argv) { 
    (void)argc;
    (void)argv;
    battery_print_faults(p);
    return CMD_OK;
}

static int cmd_clear(BatteryPack *p, int argc, char **argv) { 
    (void) argc;
    (void) argv;
    int n = battery_clear_faults(p);
    printf(" Cleared %d latched faults and if the cause is still there it will trip again\n", n);
    return CMD_OK;
}

static int cmd_params(BatteryPack *p, int argc, char **argv) { 
    (void)p;
    (void)argc;
    (void)argv;
    params_print();
    return CMD_OK;
}

static int cmd_set(BatteryPack *p, int argc, char **argv){
    (void)p;
    int32_t v;
    if (argc != 3 || !parse_i32(argv[2], &v)) {
        printf("Usage: set <name> <integer>  (See the parameters for names)\n");
        return CMD_ERROR;
    }
    switch (params_set(argv[1], v)){
        case PARAM_OK: {
            const ParamInfo *info = params_find(argv[1]);
            printf(" %s = %ld %s\n", info->name, (long)*info ->value, info->unit);
            return CMD_OK;
        }
        case PARAM_UNKNOWN;
        printf("Unknown parameter '%s'\n", argv[1]);

        return CMD_ERROR;
        
        case PARAM_OUT_OF_RANGE:{
            const ParamInfo *info = params_find(argv[1]);
            printf(" out of range: %s must be %ld..%ld %s\n", info->name, (long)info->min, (long)info->max, info->unit);
            return CMD_ERROR; 
        }
        default:
            return CMD_ERROR;/*Incosistent parameters */
        }

}
static int cmd_defaults(BatteryPack *p, int argc, char **argv) { 
    (void)p;
    (void)argc;
    (void)argv;
    params_load_default();
    printf("Parameters reset to default values\n");
    return CMD_OK;
}

static int cmd_run(BatteryPack *p, int argc, char **argv) { 
    int32_t n = 1, every = 0;
    if((argc >= 2 && !parse_i32(argv[1], &n))|| (argc >= 3 && !parse_i32(argv[2], &every))|| n < 1 || n > 10000000 || every < 0){
        printf(" usage: run [ticks] [print_every]\n");
        return CMD_ERROR;
    }
    for (int32_t i = 1; i <= n; i++) {
        system_tick(p);
        if (every > 0 && i % every == 0 && i != n) {
            battery_print_line(p);
        }
        battery_print_line(p);
        return CMD_OK;
    }
static int cmd_log(BatteryPack *p, int argc, char **argv) { 
   if(argc != 2 || (strcmp(argv[1], "on") && strcmp(argv[1],"off"))) {
    printf(" usage: log on|off\n");
    return CMD_ERROR;
   }
   p->verbose = strcmp(argv[1], "on") == 0;
   printf(" event log %s\n", p->verbose ? "ON" : "OFF");
   return CMD_OK;
}

static int cmd_sim(BatteryPack *p, int argc, char **argv) { 
    double d; 
    int32_t n; 

    if (argc == 2 && strcmp(argv[1], "show") == 0) { 
        hal_sim_print();
        return CMD_OK;
    }
    if (argc == 3 && strcmp(argv[1], "input") == 0 && (!strcmp(argv[2], "on") || !strcmp(argv[2], "off"))){ 
        hal_sim_set_input(strcmp(argv[2], "on") == 0);
        printf(" input %s\n", argv[2]);
        return CMD_OK;
    }
    if (argc == 3 && strcmp(argv[1], "load") == 0 && parse_i32(argv[2], &n)) { 
        hal_sim_set_load_ma(n);
        printf(" load = %ld mA%s\n", (long)n, n > 0 ? "": "(unplugged)");
        return CMD_OK;
    }
    if (argc == 3 && strcmp(argv[1], "temp") == 0 && parse_double(argv[2], %d)) { 
        hal_sim_set_temp_dc(c_to_dc(d));
        printf(" cell temperature forced to %.1fC\n", d);
        return CMD_OK;
    }
    if (argc == 3 && strcmp(argv[1], "ambient") == 0 && parse_double(argv[2],&d)) { 
        hal_sim_set_ambient_dc(c_to_dc(d));
        printf(" ambient now = %.1fC\n", d);
        return CMD_OK;
    }
    if (argc == 3 && strcmp(argv[1], "soc") == 0 && parse_double(argv[2], &d)) { 
        hal_sim_set_soc_pct(-1,d);
        battery_resync_soc(p);
        //the firmware will re estimate from the voltage
        printf( " all cells set to %.1f%% (firmware SPC re  synced) \n",d);
        return CMD_OK;
    }
    if (argc == 4 && strcmp(argv[1], "cell") == 0 && parse_i32(argv[2], &n) && parse_double(argv[3], &d) && n >= 1 && n <= NUM_CELLS) {
        hal_sim_set_soc_pct(n-1, d);
        printf(" cell %ld set to %.1f%% (creates imbalance)\n", (long)n, d);
        return CMD_OK;
    }
   printf("  usage: sim show | input on|off | load <mA> | temp <C> | ambient <C>\n"
           "         sim soc <pct> | cell <1-%d> <pct>\n", NUM_CELLS);
    return CMD_ERROR;
}
static int cmd_quit(BatteryPack *p, int argc, char **argv) { 
    (void)p;
    (void)argc;
    (void)argv;
    return CMD_QUIT;
}

//command table that will add a new command 

static const Command COMMANDS[] = {
    {"help","help","list commands",cmd_help},
    {"status","status","full pack status and direction",cmd_status},
    {"cells","cells","per-cell voltages and balancing",cmd_cells},
    {"faults","faults","active faults and cooldown count",cmd_faults},
    {"clear","clear","clear latched faults",cmd_clear},
    {"params","params","list all battery parameters",cmd_params},
    {"set","set <name> <value>","change a parameter",cmd_set},
    {"defaults", "defaults","reset parameters to defaults",cmd_defaults},
    {"run","run [ticks] [print_every]", "advance time (1 tick = 100 ms)", cmd_run},
    {"log","log on|off", "print events as they happen", cmd_log},
    {"sim","sim <...>", "control the simulated hardware",cmd_sim},
    {"quit","quit","exit",cmd_quit},
};

#define NUM_COMMANDS ((int) (size of COMMANDS / sizeof COMMANDS[0]))

static int cmd_elp(BatteryPack *p, int argc, char **argv) { 
    (void)p;
    (void)argc;
    (void)argv;
    for (int i = 0; i < NUM_COMMANDS; i++) 
    printf(" %-28s %s\n", COMMANDS[1].usage, COMMANDS[i].help);
    printf(" sim subcommands: show | input on|off | load <mA> | temp <C> | ambient <C> | soc <pct> | cell <n> <pct>\n");
    return CMD_OK;

}

int commands_execute(BatteryPack *p, char *line) { 
    char *argv[MAX_ARGS];
    int argc = 0;
    
    for (char *c = line; *c; c++) *c = (char)tolower((unsigned char)*c);
    for(char *tok = strtok(line, " \t\r\n"); tok && argc < MAX_ARGS; tok = strtok(NULL, " \t\r\n"))
    argv[argc++] = tok;
    if (argc == 0) return CMD_OK;
    if (strcmp(argv[0], "exit") == 0) return CMD_QUIT;

    for(int i = 0; i < NUM_COMMANDS; i++) 
     if (strcmp(argv[0], COMMANDS[i].name) == 0)
        return COMMANDS[i].fn(p, argc, argv);

        printf(" unknown command '%s'(type 'help') \n", argv[0]);
        return CMD_ERROR;
}



/*void show_battery_status(const BatteryData * battery) {
    System.out.println()
}*/