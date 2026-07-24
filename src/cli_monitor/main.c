#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fwlib32.h"
#include "cnc_ops.h"

#define DEFAULT_PORT 8193

static int print_part_count(unsigned short handle)
{
    PartCount pc;

    print_line("Part Count");

    get_part_count_on_path(handle, 0, &pc);

    printf("  Current (batch):   %ld\n", pc.current);
    printf("  Required (batch):  %ld\n", pc.required);
    printf("  Total (lifetime):  %ld\n", pc.total);

    if (pc.current >= 0 && pc.required > 0)
        printf("  Progress:          %lld%%\n", (long long)pc.current * 100 / pc.required);

    return 0;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s <IP> [options]\n\n", prog);
    printf("Options:\n");
    printf("  <port>             TCP port (default: 8193)\n");
    printf("  -show <prog>       Display program content (e.g. -show 1)\n");
    printf("  -monitor [ms]      Continuous monitoring mode (default: 1000ms)\n");
    printf("  -parts <cur> <tot> Macro vars for part count (default: #500 #501)\n");
    printf("\nExamples:\n");
    printf("  %s 192.168.1.100\n", prog);
    printf("  %s 192.168.1.100 -show 1\n", prog);
    printf("  %s 192.168.11.192 8193 -monitor 500\n", prog);
}

int main(int argc, char *argv[])
{
    unsigned short handle;
    const char *ip;
    int port;
    int monitor_mode = 0;
    int interval_ms = 1000;
    short parts_cur_var = 3901;
    short parts_total_var = 3902;
    int use_parts = 0;
    long show_prog = 0;
    int i;

    printf("============================================\n");
    printf("  CNC Monitor - FOCAS2 Communication Tool\n");
    printf("  FANUC CNC Ethernet Client\n");
    printf("============================================\n\n");

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    ip = argv[1];
    port = DEFAULT_PORT;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-monitor") == 0) {
            monitor_mode = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                interval_ms = atoi(argv[i + 1]);
                if (interval_ms <= 0) interval_ms = 1000;
                i++;
            }
        } else if (strcmp(argv[i], "-show") == 0) {
            if (i + 1 < argc) {
                show_prog = atol(argv[i + 1]);
                i++;
            }
        } else if (strcmp(argv[i], "-parts") == 0) {
            use_parts = 1;
            if (i + 2 < argc) {
                parts_cur_var = (short)atoi(argv[i + 1]);
                parts_total_var = (short)atoi(argv[i + 2]);
                i += 2;
            }
        } else if (port == DEFAULT_PORT) {
            port = atoi(argv[i]);
            if (port <= 0 || port > 65535) {
                printf("Invalid port: %s\n", argv[i]);
                return 1;
            }
        }
    }

    if (cnc_connect(ip, port, &handle) != 0)
        return 1;

    cnc_settimeout(handle, 5);

    if (monitor_mode) {
        monitor_loop(handle, interval_ms,
                     use_parts ? parts_cur_var : 0,
                     use_parts ? parts_total_var : 0);
    } else if (show_prog > 0) {
        print_program_content(handle, show_prog);
    } else {
        print_path_info(handle);
        print_system_info(handle);
        print_status(handle);
        print_actf_acts(handle);
        print_positions(handle);
        print_dynamic(handle);
        print_alarms(handle);
        print_program_info(handle);
        print_program_list(handle);
        print_part_count(handle);
        
        print_macro_variables(handle);
        print_tool_offsets(handle);
        print_work_zero_offsets(handle);
        print_parameters(handle);
    }

    cnc_disconnect(handle);
    return 0;
}
