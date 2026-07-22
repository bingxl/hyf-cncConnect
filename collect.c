#include <stdio.h>
#include <stdlib.h>
#include "fwlib32.h"
#include "cnc_ops.h"
#include "file_io.h"

int main(void)
{
    MachineInfo machines[MAX_MACHINES];
    int count, i;
    FILE *result;

    printf("============================================\n");
    printf("  CNC Batch Data Collector\n");
    printf("============================================\n\n");

    if (parse_jichuang("jichuang.txt", machines, &count) != 0) {
        return 1;
    }
    printf("Loaded %d machine(s) from jichuang.txt\n\n", count);

    result = fopen("result.txt", "w");
    if (!result) {
        printf("[ERROR] Cannot open result.txt for writing\n");
        return 1;
    }

    fprintf(result, "MachineName, SaveTime, TotalRequired, CurrentBatch, LifetimeTotal\n");

    for (i = 0; i < count; i++) {
        MachineInfo *m = &machines[i];
        unsigned short handle;
        PartCount pc;

        printf("[%d/%d] %s (%s:%d) ... ", i + 1, count,
               m->name, m->ip, m->port);

        if (cnc_connect(m->ip, m->port, &handle) != 0) {
            printf("CONNECT FAILED\n");
            write_result(result, m->name, -1, -1, -1);
            continue;
        }

        cnc_settimeout(handle, 5);
        pc.current = -1; pc.required = -1; pc.total = -1;
        read_counts(handle, &pc);
        cnc_disconnect(handle);

        printf("OK (required=%ld, current=%ld, total=%ld)\n",
               pc.required, pc.current, pc.total);

        write_result(result, m->name, pc.required, pc.current, pc.total);
    }

    fclose(result);
    printf("\nDone. Results saved to result.txt\n");
    return 0;
}
