#ifndef CNC_OPS_H
#define CNC_OPS_H

#include "fwlib32.h"

typedef struct {
    long current;
    long required;
    long total;
} PartCount;

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

#endif
