#ifndef CNC_OPS_H
#define CNC_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "fwlib32.h"

#define CNC_MAX_AXES    9
#define CNC_MAX_ALARMS  10
#define CNC_MAX_PROGRAMS 100
#define CNC_MAX_MACRO   10

typedef struct {
    long current;
    long required;
    long total;
} PartCount;

typedef struct {
    char cnc_type[3];
    char mt_type[3];
    char series[5];
    char version[5];
    int  max_axis;
    char axes[3];
} CncSystemInfo;

typedef struct {
    int tmmode;
    int aut;
    int run;
    int motion;
    int mstb;
    int emergency;
    int alarm;
    int edit;
} CncStatus;

typedef struct {
    int  count;
    double absolute[CNC_MAX_AXES];
    double machine[CNC_MAX_AXES];
    double relative[CNC_MAX_AXES];
    double distance[CNC_MAX_AXES];
} CncPositions;

typedef struct {
    int count;
    long alarm_no[CNC_MAX_ALARMS];
    char axis[CNC_MAX_ALARMS];
    char msg[CNC_MAX_ALARMS][32];
} CncAlarms;

typedef struct {
    long feedrate;
    long spindle;
} CncActData;

typedef struct {
    int  prg_number;
    int  prg_main;
    char prg_name[36];
    char comment[36];
    long seq_number;
    long blk_count;
} CncProgramInfo;

typedef struct {
    long actf;
    long acts;
    long alarm;
    long prgnum;
    long prgmnum;
    long seqnum;
    int  axis;
} CncDynamicData;

typedef struct {
    int count;
    long number[CNC_MAX_PROGRAMS];
    long length[CNC_MAX_PROGRAMS];
    char comment[CNC_MAX_PROGRAMS][36];
} CncProgramList;

typedef struct {
    double value;
} CncMacroVar[CNC_MAX_MACRO];

typedef struct {
    double values[CNC_MAX_MACRO];
    int count;
} CncMacroData;

#define CNC_MAX_TOOL 10
typedef struct {
    double values[CNC_MAX_TOOL];
    int count;
} CncToolOffsetData;

typedef struct {
    double values[CNC_MAX_AXES];
    int count;
} CncWorkZeroData;

typedef struct {
    int ok;
    CncSystemInfo sys;
    CncStatus status;
    CncPositions pos;
    CncAlarms alarms;
    CncActData act;
    CncProgramInfo prog;
    CncDynamicData dyn;
    PartCount part_count;
    CncProgramList prog_list;
    CncMacroData macro_vars;
    CncToolOffsetData tool_offsets;
    CncWorkZeroData work_zero;
    long param_6750;
    long setting_0;
    short path_current;
    short path_count;
    char error_msg[128];
} CncMachineData;

const char* focas_error(short ret);
void print_line(const char *title);
int cnc_connect(const char *ip, int port, unsigned short *handle);
void cnc_disconnect(unsigned short handle);

int print_system_info(unsigned short handle);
int print_status(unsigned short handle);
int print_positions(unsigned short handle);
int print_alarms(unsigned short handle);
int print_actf_acts(unsigned short handle);
int print_program_info(unsigned short handle);
int print_dynamic(unsigned short handle);
int print_program_list(unsigned short handle);
int print_program_content(unsigned short handle, long prog_no);
int print_path_info(unsigned short handle);
int print_macro_variables(unsigned short handle);
int print_tool_offsets(unsigned short handle);
int print_work_zero_offsets(unsigned short handle);
int print_parameters(unsigned short handle);

int read_counts(unsigned short handle, PartCount *pc);
int get_part_count(unsigned short handle, PartCount *pc);
int get_part_count_on_path(unsigned short handle, short path, PartCount *pc);

int monitor_loop(unsigned short handle, int interval_ms,
                 short parts_cur_var, short parts_total_var);

int fetch_system_info(unsigned short handle, CncSystemInfo *info);
int fetch_status(unsigned short handle, CncStatus *status);
int fetch_positions(unsigned short handle, CncPositions *pos);
int fetch_alarms(unsigned short handle, CncAlarms *alarms);
int fetch_act_data(unsigned short handle, CncActData *act);
int fetch_program_info(unsigned short handle, CncProgramInfo *prog);
int fetch_dynamic(unsigned short handle, CncDynamicData *dyn);
int fetch_program_list(unsigned short handle, CncProgramList *list);
int get_program_comment(unsigned short handle, long prog_no, char *out, int size);
int fetch_macro_vars(unsigned short handle, CncMacroData *data);
int fetch_tool_offsets(unsigned short handle, CncToolOffsetData *data);
int fetch_work_zero(unsigned short handle, CncWorkZeroData *data);
int fetch_parameters(unsigned short handle, long *param_6750, long *setting_0);
int fetch_path_info(unsigned short handle, short *current, short *count);
int fetch_machine_data(const char *ip, int port, CncMachineData *data);
int bench_program_comment(const char *ip, int port, int iterations);

#ifdef __cplusplus
}
#endif

#endif
