/*
 * genconfig.c - Generate MTConnect agent configuration from a machine list.
 *
 * Machine list (v2) format:  name,type,ip,port[,config]
 *   type: FANUC | MAZAK | SIM | SHDR
 *     FANUC  -> fanuc_adapter.exe  (FOCAS2 直连, 输出本地 SHDR)
 *     MAZAK  -> mazak_adapter.exe  (MTConnect 拉取, 输出本地 SHDR)
 *     SIM    -> shdr_sim.exe       (离线模拟)
 *     SHDR   -> agent 直连远程 SHDR 端口 (无本地进程)
 *   config : 可选的独立配置文件名 (adapters/<name>/<config>, 如 adapter.ini)
 *
 * Device models are rendered from devices/<type>.xml templates
 * (placeholders %NAME% %UUID% %IP% %PORT% %SHDRPORT%).
 *
 * Usage:
 *   genconfig.exe <jichuang.txt> <out_dir> [http_port] [shdr_base_port] [shdr_host]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_MACHINES 128
#define MAX_LINE 512
#define MAX_TEMPLATE (128 * 1024)

typedef struct {
    char name[64];
    char type[16];      /* FANUC / MAZAK / SIM / SHDR */
    char ip[64];
    int  port;          /* system port (FOCAS / MTConnect / remote SHDR) */
    int  shdr_port;     /* local SHDR port (for FANUC/MAZAK/SIM) */
    char config[256];   /* per-machine config file name (optional) */
} Machine;

/* ---------- small helpers ---------- */
static char *slurp(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > MAX_TEMPLATE) { fclose(f); return NULL; }
    buf = (char *)malloc(n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = '\0';
    fclose(f);
    if (size) *size = n;
    return buf;
}

/* replace all occurrences of a placeholder with a value (in place-safe way) */
static char *tpl_replace(const char *tpl, const char *ph, const char *val)
{
    size_t plen = strlen(ph), vlen = strlen(val), tlen = strlen(tpl);
    size_t count = 0, i;
    for (i = 0; i + plen <= tlen; i++)
        if (strncmp(tpl + i, ph, plen) == 0) count++;
    if (count == 0) return _strdup(tpl);

    char *out = (char *)malloc(tlen + count * (vlen - plen) + 1);
    if (!out) return NULL;
    char *o = out;
    const char *p = tpl, *m;
    while ((m = strstr(p, ph)) != NULL) {
        size_t pre = (size_t)(m - p);
        memcpy(o, p, pre);
        o += pre;
        memcpy(o, val, vlen);
        o += vlen;
        p = m + plen;
    }
    strcpy(o, p);
    return out;
}
/* ---------- machine list parsing (v2) ---------- */
static int read_machines(const char *path, Machine *m, int max)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "[genconfig] cannot open %s\n", path); return -1; }

    char line[MAX_LINE];
    int n = 0;
    while (fgets(line, sizeof(line), f) && n < max) {
        char *s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
        if (*s == '\0' || *s == '#') continue;

        char *p = strrchr(s, '\n'); if (p) *p = '\0';
        p = strrchr(s, '\r'); if (p) *p = '\0';

        char name[64], type[16], ip[64], cfg[256];
        int port = 0;
        memset(cfg, 0, sizeof(cfg));
        int fields = sscanf(s, "%63[^,],%15[^,],%63[^,],%d,%255[^\n]", name, type, ip, &port, cfg);
        if (fields < 4) {
            fprintf(stderr, "[genconfig] skip bad line: %s\n", s);
            continue;
        }
        /* normalize type */
        for (p = type; *p; p++) *p = (char)toupper((unsigned char)*p);

        if (strcmp(type, "FANUC") != 0 && strcmp(type, "MAZAK") != 0 &&
            strcmp(type, "SIM") != 0 && strcmp(type, "SHDR") != 0) {
            fprintf(stderr, "[genconfig] skip unknown type '%s' in line: %s\n", type, s);
            continue;
        }
        strncpy(m[n].name, name, sizeof(m[n].name) - 1);
        strncpy(m[n].type, type, sizeof(m[n].type) - 1);
        strncpy(m[n].ip, ip, sizeof(m[n].ip) - 1);
        m[n].port = port;
        if (cfg[0]) strncpy(m[n].config, cfg, sizeof(m[n].config) - 1);
        n++;
    }
    fclose(f);
    return n;
}

/* ---------- per-machine adapter.ini (FANUC) ---------- */
static void write_machine_ini(const char *out_dir, const char *tpl_dir, const Machine *m)
{
    char dir[1024], tmpl[1024], ini[1024], buf[MAX_TEMPLATE];
    FILE *f;
    long n;

    snprintf(dir, sizeof(dir), "%s/adapters", out_dir);
    _mkdir(dir);
    snprintf(dir, sizeof(dir), "%s/adapters/%s", out_dir, m->name);
    _mkdir(dir);
    snprintf(ini, sizeof(ini), "%s/adapter.ini", dir);

    /* per-machine config override, else builtin default */
    if (m->config[0]) {
        snprintf(tmpl, sizeof(tmpl), "%s/%s", out_dir, m->config);
        f = fopen(tmpl, "rb");
        if (f) {
            n = (long)fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            fclose(f);
        } else {
            fprintf(stderr, "[genconfig] config '%s' not found, using default\n", m->config);
            goto default_ini;
        }
    } else {
default_ini:
        snprintf(tmpl, sizeof(tmpl), "%s/fanuc.ini", tpl_dir);
        f = fopen(tmpl, "rb");
        if (f) {
            n = (long)fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            fclose(f);
        } else {
            /* builtin default (same as shipped adapter.ini) */
            snprintf(buf, sizeof(buf),
                "[macros]\n"
                "probe = [130 131 132]\n"
                "part = 140\n"
                "part_current = 3901\n"
                "part_required = 3902\n"
                "\n"
                "[pmc]\n"
                "SspeedOvr = 30\n"
                "Fovr = 12\n"
                "\n"
                "[params]\n"
                "part_total = 6712\n");
        }
    }

    f = fopen(ini, "w");
    if (!f) { fprintf(stderr, "[genconfig] cannot write %s\n", ini); return; }
    fputs(buf, f);
    fclose(f);
}
/* ---------- Devices.xml (template driven, one <Device> per machine) ---------- */
static void write_devices_xml(const char *out_dir, const char *tpl_dir, Machine *m, int count)
{
    char xmlpath[1024], tplpath[1024];
    FILE *f;
    int i;

    snprintf(xmlpath, sizeof(xmlpath), "%s/Devices.xml", out_dir);
    f = fopen(xmlpath, "w");
    if (!f) { fprintf(stderr, "[genconfig] cannot write %s\n", xmlpath); return; }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<MTConnectDevices xmlns=\"urn:mtconnect.org:MTConnectDevices:1.8\"\n");
    fprintf(f, "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
    fprintf(f, "  xsi:schemaLocation=\"urn:mtconnect.org:MTConnectDevices:1.8 http://schemas.mtconnect.org/schemas/MTConnectDevices_1.8.xsd\">\n");
    fprintf(f, "  <Header creationTime=\"2024-01-01T00:00:00Z\" sender=\"mtconnect_collect\" instanceId=\"1\" version=\"1.8\" bufferSize=\"131072\"/>\n");
    fprintf(f, "  <Devices>\n");

    for (i = 0; i < count; i++) {
        /* type -> template file (lowercased) */
        char tname[16];
        long n;
        char *tpl, *r1, *r2, *r3, *r4, *r5;
        for (n = 0; m[i].type[n]; n++) {
            char c = m[i].type[n];
            tname[n] = (char)tolower((unsigned char)c);
        }
        tname[n] = '\0';
        snprintf(tplpath, sizeof(tplpath), "%s/%s.xml", tpl_dir, tname);
        tpl = slurp(tplpath, &n);
        if (!tpl) {
            fprintf(stderr, "[genconfig] template %s not found for %s, skipping\n",
                    tplpath, m[i].name);
            continue;
        }
        {
            char portbuf[32], shdrbuf[32];
            snprintf(portbuf, sizeof(portbuf), "%d", m[i].port);
            snprintf(shdrbuf, sizeof(shdrbuf), "%d", m[i].shdr_port);
            r1 = tpl_replace(tpl, "%NAME%", m[i].name);
            r2 = tpl_replace(r1 ? r1 : tpl, "%UUID%", m[i].name);
            r3 = tpl_replace(r2 ? r2 : tpl, "%IP%", m[i].ip);
            r4 = tpl_replace(r3 ? r3 : tpl, "%PORT%", portbuf);
            r5 = tpl_replace(r4 ? r4 : tpl, "%SHDRPORT%", shdrbuf);
        }
        if (r5) {
            fputs("    ", f);
            fputs(r5, f);
            fputc('\n', f);
        }
        free(r1); free(r2); free(r3); free(r4); free(r5);
        free(tpl);
    }

    fprintf(f, "  </Devices>\n");
    fprintf(f, "</MTConnectDevices>\n");
    fclose(f);
    printf("[genconfig] wrote %s (%d devices)\n", xmlpath, count);
}
/* ---------- agent.cfg + adapters.txt ---------- */
static void write_agent_cfg(const char *out_dir, Machine *m, int count,
                            int http_port, const char *shdr_host)
{
    char path[1024];
    FILE *f;
    int i;

    snprintf(path, sizeof(path), "%s/agent.cfg", out_dir);
    f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[genconfig] cannot write %s\n", path); return; }

    fprintf(f, "# Generated by genconfig.exe - MTConnect agent configuration\n");
    fprintf(f, "Devices = Devices.xml\n");
    fprintf(f, "SchemaVersion = 2.7\n");
    fprintf(f, "WorkerThreads = 3\n");
    fprintf(f, "Port = %d\n", http_port);
    fprintf(f, "BufferSize = 17\n");
    fprintf(f, "MonitorConfigFiles = no\n");
    fprintf(f, "CreateUniqueIds = true\n\n");

    fprintf(f, "HttpHeaders {\n");
    fprintf(f, "\tAccess-Control-Allow-Origin = *\n");
    fprintf(f, "\tAccess-Control-Allow-Methods = GET\n");
    fprintf(f, "}\n\n");

    fprintf(f, "Adapters {\n");
    for (i = 0; i < count; i++) {
        fprintf(f, "  %s {\n", m[i].name);
        if (strcmp(m[i].type, "SHDR") == 0) {
            /* passthrough: agent connects straight to the remote SHDR */
            fprintf(f, "    Host = %s\n", m[i].ip);
            fprintf(f, "    Port = %d\n", m[i].port);
        } else {
            fprintf(f, "    Host = %s\n", shdr_host);
            fprintf(f, "    Port = %d\n", m[i].shdr_port);
        }
        fprintf(f, "  }\n");
    }
    fprintf(f, "}\n\n");

    fprintf(f, "logger_config {\n");
    fprintf(f, "  output = file ../log/agent.log\n");
    fprintf(f, "  level = info\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("[genconfig] wrote %s (%d adapters)\n", path, count);

    /* adapters.txt : launcher list  type name ip port shdr_port */
    snprintf(path, sizeof(path), "%s/adapters.txt", out_dir);
    f = fopen(path, "w");
    if (!f) return;
    for (i = 0; i < count; i++)
        fprintf(f, "%s %s %s %d %d\n", m[i].type, m[i].name, m[i].ip,
                m[i].port, m[i].shdr_port);
    fclose(f);
    printf("[genconfig] wrote %s\n", path);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <jichuang.txt> <out_dir> [http_port] [shdr_base_port] [shdr_host]\n", argv[0]);
        printf("  jichuang.txt format:  name,type,ip,port[,config]\n");
        printf("  type: FANUC | MAZAK | SIM | SHDR\n");
        return 1;
    }

    int http_port = argc > 3 ? atoi(argv[3]) : 5000;
    int base_port = argc > 4 ? atoi(argv[4]) : 7878;
    const char *shdr_host = argc > 5 ? argv[5] : "127.0.0.1";
    const char *tpl_dir = argc > 6 ? argv[6] : "devices";

    Machine machines[MAX_MACHINES];
    int count = read_machines(argv[1], machines, MAX_MACHINES);
    if (count <= 0) {
        fprintf(stderr, "[genconfig] no machines found in %s\n", argv[1]);
        return 1;
    }
    for (int i = 0; i < count; i++) {
        /* SHDR passthrough uses its remote port directly, no local port */
        if (strcmp(machines[i].type, "SHDR") == 0)
            machines[i].shdr_port = machines[i].port;
        else
            machines[i].shdr_port = base_port + i;
        if (strcmp(machines[i].type, "FANUC") == 0)
            write_machine_ini(argv[2], tpl_dir, &machines[i]);
    }

    write_devices_xml(argv[2], tpl_dir, machines, count);
    write_agent_cfg(argv[2], machines, count, http_port, shdr_host);

    printf("[genconfig] machine list:\n");
    for (int i = 0; i < count; i++)
        printf("  %-8s %-6s %s:%d -> SHDR %d%s\n", machines[i].name, machines[i].type,
               machines[i].ip, machines[i].port, machines[i].shdr_port,
               machines[i].config[0] ? " (custom config)" : "");
    return 0;
}
