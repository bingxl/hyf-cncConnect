/*
 * cnc_sampler - resident CNC sampling daemon.
 *
 * Reads the machine list (jichuang.txt) and periodically samples each
 * machine's status / program / part count, writing rows into
 * machine_samples and upserting into machine_latest in the shared DB
 * (%USERPROFILE%\data-collect\cnc_monitor.db).
 *
 * Keeps one long-lived FOCAS connection per machine (only closed on exit)
 * and reconnects with exponential backoff on failures.
 *
 * Usage:
 *   cnc_sampler.exe [-interval <secs>] [-log <path>]
 *
 *   -interval  Sample interval in seconds (default 5)
 *   -log       Absolute path to the log file (default: data-collect\cnc_sampler.log)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <windows.h>

#include "fwlib32.h"
#include "cnc_ops.h"
#include "file_io.h"
#include "db_ops.h"

#define SAMPLER_MAX_MACHINES 64
#define MAX_BACKOFF_MS       60000
#define DEFAULT_INTERVAL     5
#define DEFAULT_RETENTION    90
#define SAMPLER_CONNECT_TIMEOUT 4   /* secs: quick fail so a dead machine doesn't stall the loop */
#define SAMPLER_IO_TIMEOUT        4   /* secs: per-FOCAS-read timeout */

typedef struct {
    int    interval;         /* sample interval, seconds */
    int    retention_days;   /* keep this many days of samples */
} SamplerConfig;

typedef struct {
    MachineInfo    info;
    int            machine_id;    /* DB machines table id */
    unsigned short handle;        /* live FOCAS handle (0 = not connected) */
    unsigned       backoff_ms;    /* current reconnect backoff */
    long           next_attempt;  /* epoch seconds before reconnect allowed */
} SamplerMachine;

static DbHandle g_db = NULL;
static char g_log_path[512] = { 0 };

static const char *iso_timestamp(void)
{
    static char buf[40];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return buf;
}

static void log_msg(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    FILE *fp;

    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    fp = g_log_path[0] ? fopen(g_log_path, "a") : NULL;
    if (fp) {
        fprintf(fp, "[%s] %s\n", iso_timestamp(), buf);
        fclose(fp);
    }
    printf("[%s] %s\n", iso_timestamp(), buf);
    fflush(stdout);
}

/* Config file: data-collect\config.txt
     # comment
     interval=10
     retention_days=90
   Returns 0 on success (creates default file if missing). */
static int load_config(const char *path, SamplerConfig *cfg)
{
    FILE *fp;
    char line[256];
    int created = 0;

    cfg->interval = DEFAULT_INTERVAL;
    cfg->retention_days = DEFAULT_RETENTION;

    fp = fopen(path, "r");
    if (!fp) {
        /* seed a default config */
        fp = fopen(path, "w");
        if (!fp) return -1;
        fprintf(fp, "# CNC sampler config\n");
        fprintf(fp, "interval=%d\n", DEFAULT_INTERVAL);
        fprintf(fp, "retention_days=%d\n", DEFAULT_RETENTION);
        fprintf(fp, "# machine list is read from jichuang.txt\n");
        fclose(fp);
        created = 1;
    } else {
        while (fgets(line, sizeof(line), fp)) {
            char key[64], val[64];
            char *eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            sscanf(line, "%63[^=\r\n]=%63[^\r\n]", key, val);
            if (strcmp(key, "interval") == 0)
                cfg->interval = atoi(val);
            else if (strcmp(key, "retention_days") == 0)
                cfg->retention_days = atoi(val);
        }
        fclose(fp);
    }

    if (cfg->interval < 1) cfg->interval = DEFAULT_INTERVAL;
    if (cfg->retention_days < 1) cfg->retention_days = DEFAULT_RETENTION;

    if (created)
        log_msg("Created default config file %s", path);
    return 0;
}

static int lookup_machine_id(const char *name)
{
    MachineRecord recs[SAMPLER_MAX_MACHINES];
    int n = db_get_machines(g_db, recs, SAMPLER_MAX_MACHINES);
    int i;
    if (n < 0) n = 0;
    for (i = 0; i < n; i++) {
        if (strcmp(recs[i].name, name) == 0)
            return recs[i].id;
    }
    return -1;
}

static void register_machine(SamplerMachine *m)
{
    int id = lookup_machine_id(m->info.name);
    if (id < 0) {
        if (db_add_machine(g_db, m->info.name, m->info.ip, m->info.port) == 0)
            id = lookup_machine_id(m->info.name);
    }
    m->machine_id = id;
    if (id > 0)
        log_msg("Machine %s -> id=%d", m->info.name, id);
    else
        log_msg("Machine %s not registered", m->info.name);
}

/* Returns 1 if a live handle is available (connects on demand). */
static int ensure_connected(SamplerMachine *m)
{
    if (m->handle != 0)
        return 1;

    if (m->next_attempt > (long)time(NULL))
        return 0;   /* still in backoff window */

    /* Short connect timeout so one unreachable machine does not stall the
       whole sampling loop (which is serial across machines). */
    if (cnc_connect_timeout(m->info.ip, m->info.port, SAMPLER_CONNECT_TIMEOUT,
                            &m->handle) != 0) {
        m->handle = 0;
        /* schedule next retry after current backoff */
        m->next_attempt = (long)time(NULL) + (long)(m->backoff_ms / 1000);
        if (m->backoff_ms == 0) m->backoff_ms = 1000;
        return 0;
    }
    cnc_settimeout(m->handle, SAMPLER_IO_TIMEOUT);
    m->backoff_ms = 1000;
    m->next_attempt = 0;
    log_msg("Connected %s (id=%d)", m->info.name, m->machine_id);
    return 1;
}

static void drop_connection(SamplerMachine *m)
{
    if (m->handle != 0) {
        cnc_freelibhndl(m->handle);
        m->handle = 0;
    }
    /* exponential backoff for reconnect scheduling */
    if (m->backoff_ms == 0) m->backoff_ms = 1000;
    else if (m->backoff_ms < MAX_BACKOFF_MS) m->backoff_ms *= 2;
    m->next_attempt = (long)time(NULL) + (long)(m->backoff_ms / 1000);
    log_msg("Connection lost for %s; backoff=%ums", m->info.name, m->backoff_ms);
}

/* Samples one machine at a time and returns 0 on connection failure. */
static int sample_machine(SamplerMachine *m)
{
    MachineSample s;
    CncStatus status;
    CncProgramInfo prog;
    PartCount pc;
    char comment[DB_MAX_COMMENT] = "";
    long now_ts;
    int status_ok, prog_ok;

    if (m->machine_id <= 0 || m->handle == 0)
        return 0;

    now_ts = (long)time(NULL);

    memset(&status, 0, sizeof(status));
    memset(&prog, 0, sizeof(prog));
    status_ok = (fetch_status(m->handle, &status) == 0);
    prog_ok = (fetch_program_info(m->handle, &prog) == 0);

    pc.current = -1;
    pc.required = -1;
    pc.total = -1;
    if (status_ok)
        get_part_count(m->handle, &pc);

    if (prog_ok && prog.prg_number > 0)
        get_program_comment(m->handle, prog.prg_number, comment, sizeof(comment));

    memset(&s, 0, sizeof(s));
    s.machine_id = m->machine_id;
    s.ts = now_ts;
    s.run = status_ok ? status.run : -1;
    s.aut = status_ok ? status.aut : -1;
    s.tmmode = status_ok ? status.tmmode : -1;
    s.program_no = prog_ok ? prog.prg_number : 0;
    strncpy(s.program_comment, comment, DB_MAX_COMMENT - 1);
    s.program_comment[DB_MAX_COMMENT - 1] = 0;
    s.part_total = status_ok ? pc.total : -1;
    s.part_current = status_ok ? pc.current : -1;
    s.part_required = status_ok ? pc.required : -1;

    if (!status_ok) {
        /* the connection is suspect; force an error path */
        return -1;
    }

    {
        MachineLatest l;
        int attempt;
        int add_ok = 0, upsert_ok = 0;

        memset(&l, 0, sizeof(l));
        l.machine_id = s.machine_id;
        l.ts = s.ts;
        l.run = s.run;
        l.aut = s.aut;
        l.tmmode = s.tmmode;
        l.program_no = s.program_no;
        strncpy(l.program_comment, s.program_comment, DB_MAX_COMMENT - 1);
        l.part_total = s.part_total;
        l.part_current = s.part_current;
        l.part_required = s.part_required;

        for (attempt = 0; attempt < 2 && (!add_ok || !upsert_ok); attempt++) {
            if (!add_ok && db_add_sample(g_db, &s) == 0)
                add_ok = 1;
            if (!upsert_ok && db_upsert_machine_latest(g_db, &l) == 0)
                upsert_ok = 1;
            if (!add_ok || !upsert_ok)
                Sleep(150);   /* transient DB failure (busy/IO): brief retry */
        }
        if (!add_ok)
            log_msg("  %s: db_add_sample failed (ts=%ld) - sample lost", m->info.name, now_ts);
        if (!upsert_ok)
            log_msg("  %s: db_upsert_machine_latest failed (ts=%ld)",
                    m->info.name, now_ts);
    }

    return 0;
}

static void prune_old_samples(int retention_days, const char *data_dir)
{
    long cutoff = (long)(time(NULL) - (long)retention_days * 24 * 3600);
    long prev = db_get_pruned_before(g_db);
    long n;

    if (cutoff <= prev)
        return;   /* nothing new to archive since last run */

    n = db_count_samples_older_than(g_db, cutoff);
    if (n <= 0)
        return;

    /* archive db: data-collect\archive\archive_YYYYMMDD.db */
    {
        char archive_dir[512];
        char archive_path[576];
        _snprintf(archive_dir, sizeof(archive_dir), "%s\\archive", data_dir);
        CreateDirectoryA(archive_dir, NULL);

        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        _snprintf(archive_path, sizeof(archive_path), "%s\\archive_%04d%02d%02d.db",
                  archive_dir, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

        long moved = db_archive_samples_older_than(g_db, archive_path, cutoff);
        db_set_pruned_before(g_db, cutoff);
        log_msg("Archived %ld samples (>%d days) to %s", moved, retention_days, archive_path);
    }
}

int main(int argc, char **argv)
{
    SamplerMachine machines[SAMPLER_MAX_MACHINES];
    MachineInfo     raw_machines[SAMPLER_MAX_MACHINES];
    SamplerConfig   cfg;
    char data_dir[512];
    char jichuang_path[512];
    char config_path[512];
    char db_path[512];
    int machine_count = 0;
    int i;
    long last_prune = 0;

    /* --- data dir --- */
    {
        const char *profile = getenv("USERPROFILE");
        if (!profile) profile = ".";
        _snprintf(data_dir, sizeof(data_dir), "%s\\data-collect", profile);
        CreateDirectoryA(data_dir, NULL);

        _snprintf(jichuang_path, sizeof(jichuang_path), "%s\\jichuang.txt", data_dir);
        if (GetFileAttributesA(jichuang_path) == INVALID_FILE_ATTRIBUTES) {
            _snprintf(jichuang_path, sizeof(jichuang_path), "jichuang.txt");
        }
        _snprintf(config_path, sizeof(config_path), "%s\\config.txt", data_dir);
        _snprintf(db_path, sizeof(db_path), "%s\\cnc_monitor.db", data_dir);
        if (!g_log_path[0]) {
            _snprintf(g_log_path, sizeof(g_log_path), "%s\\cnc_sampler.log", data_dir);
        }
    }

    /* --- config --- */
    load_config(config_path, &cfg);
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-interval") == 0 && i + 1 < argc) {
                cfg.interval = atoi(argv[++i]);
                if (cfg.interval < 1) cfg.interval = 1;
            } else if (strcmp(argv[i], "-log") == 0 && i + 1 < argc) {
                strncpy(g_log_path, argv[++i], sizeof(g_log_path) - 1);
                g_log_path[sizeof(g_log_path) - 1] = 0;
            }
        }
    }

    printf("CNC Sampler\n");
    printf("  data dir : %s\n", data_dir);
    printf("  db       : %s\n", db_path);
    printf("  cfg      : %s (interval=%ds, retention=%ddays)\n",
           config_path, cfg.interval, cfg.retention_days);

    /* --- machines --- */
    if (parse_jichuang(jichuang_path, raw_machines, &machine_count) != 0) {
        printf("[FATAL] cannot parse %s\n", jichuang_path);
        return 1;
    }
    if (machine_count == 0 || machine_count > SAMPLER_MAX_MACHINES) {
        printf("[FATAL] bad machine count %d\n", machine_count);
        return 1;
    }
    for (i = 0; i < machine_count; i++) {
        machines[i].info = raw_machines[i];
        machines[i].machine_id = 0;
        machines[i].handle = 0;
        machines[i].backoff_ms = 0;
        machines[i].next_attempt = 0;
    }

    /* --- DB --- */
    g_db = db_open(db_path);
    if (!g_db) {
        printf("[FATAL] cannot open DB %s\n", db_path);
        return 1;
    }
    if (db_init_tables(g_db) != 0) {
        printf("[FATAL] db_init_tables failed\n");
        db_close(g_db);
        return 1;
    }

    for (i = 0; i < machine_count; i++) {
        register_machine(&machines[i]);
    }

    log_msg("Sampler started: %d machines, interval %ds, retention %dd",
            machine_count, cfg.interval, cfg.retention_days);

    /* --- main loop --- */
    while (1) {
        long now = (long)time(NULL);

        for (i = 0; i < machine_count; i++) {
            SamplerMachine *m = &machines[i];

            if (!ensure_connected(m))
                continue;

            if (sample_machine(m) != 0)
                drop_connection(m);
        }

        if (now - last_prune >= 3600) {
            prune_old_samples(cfg.retention_days, data_dir);
            last_prune = now;
        }

        Sleep(cfg.interval * 1000);
    }

    db_close(g_db);
    return 0;
}
