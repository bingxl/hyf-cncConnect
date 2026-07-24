#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdio.h>

#define MAX_MACHINES 64
#define LINE_BUF_SIZE 256

typedef struct {
    char name[64];
    char ip[64];
    int port;
} MachineInfo;

int parse_jichuang(const char *path, MachineInfo *machines, int *count);
void write_result(FILE *fp, const char *machine_name,
                  long required, long current, long total);

#endif
