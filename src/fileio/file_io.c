#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "file_io.h"

int parse_jichuang(const char *path, MachineInfo *machines, int *count)
{
    FILE *fp;
    char line[LINE_BUF_SIZE];
    int n = 0;

    fp = fopen(path, "r");
    if (!fp) {
        printf("[ERROR] Cannot open %s\n", path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp) && n < MAX_MACHINES) {
        char *p = line;
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (*p == '\0' || *p == '\n' || *p == '#')
            continue;

        MachineInfo *m = &machines[n];
        memset(m, 0, sizeof(MachineInfo));

        if (sscanf(p, "%63[^,],%63[^,],%d",
                m->name, m->ip, &m->port) >= 3) {
            n++;
        }
    }
    fclose(fp);
    *count = n;
    return 0;
}

void write_result(FILE *fp, const char *machine_name,
                  long required, long current, long total)
{
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char time_str[32];

    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_now);
    fprintf(fp, "%s, %s, %ld, %ld, %ld\n",
            machine_name, time_str, required, current, total);
    fflush(fp);
}
