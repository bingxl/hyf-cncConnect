/*
 * mtc_stats.c - MTConnect 数据采集统计工具
 *
 *   poll   : 周期性读取 agent /current 快照并存入 SQLite
 *   report : 按时间/机器/产品统计运行时间与产量
 *
 * Usage:
 *   mtc_stats.exe poll [http_port] [interval_sec] [db]
 *   mtc_stats.exe report [db] [bucket_sec] [from_unix] [to_unix]
 *
 * 统计口径（与现有 cnc_sampler 一致）：
 *   加工中 = execution==ACTIVE && mode==AUTOMATIC && tmmode==0
 *   产量   = part_total (#6712) 差值
 *   产品   = program comment
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <winhttp.h>
#include "sqlite3.h"

#define MAX_MACHINES 64
#define MAX_NAME 64
#define MAX_VALUE 256

#pragma comment(lib, "winhttp.lib")

typedef struct {
    char name[MAX_NAME];
    char execution[32];
    char mode[32];
    char tmmode[16];
    char program[32];
    char comment[MAX_VALUE];
    long long part_total;
    long long ts;
    int valid;
} MachineSample;

/* ---------- HTTP GET (WinHTTP) ---------- */
static char *http_get(int port, const char *path, int *out_len)
{
    BOOL ok = FALSE;
    HINTERNET hSession = NULL, hConn = NULL, hReq = NULL;
    char *buf = NULL;
    *out_len = 0;

    hSession = WinHttpOpen(L"mtc-stats/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;

    wchar_t host[] = L"127.0.0.1";
    hConn = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConn) goto done;

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    hReq = WinHttpOpenRequest(hConn, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                              WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hReq) goto done;

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto done;
    if (!WinHttpReceiveResponse(hReq, NULL)) goto done;

    DWORD avail = 0, total = 0, read = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        char *nb = (char *)realloc(buf, total + avail + 1);
        if (!nb) goto done;
        buf = nb;
        if (!WinHttpReadData(hReq, buf + total, avail, &read)) goto done;
        total += read;
        if (read < avail) break;
    }
    if (buf) buf[total] = '\0';
    *out_len = (int)total;
    ok = TRUE;

done:
    if (hReq) WinHttpCloseHandle(hReq);
    if (hConn) WinHttpCloseHandle(hConn);
    if (hSession) WinHttpCloseHandle(hSession);
    if (!ok && buf) { free(buf); buf = NULL; *out_len = 0; }
    return buf;
}
/* find element with explicit open/close strings (e.g. open="<Program " so it
   does not match "<ProgramComment") */
static char *find_element2(char **pp, const char *open, const char *close,
                           char *out, int outsz)
{
    char *start = strstr(*pp, open);
    char *end;
    char *v;
    size_t n;
    if (!start) return NULL;
    end = strstr(start, close);
    if (!end) return NULL;
    v = strchr(start, '>');
    if (!v || v > end) return NULL;
    v++;
    n = (size_t)(end - v);
    if (n >= (size_t)outsz) n = outsz - 1;
    memcpy(out, v, n);
    out[n] = '\0';
    *pp = end + strlen(close);
    return out;
}
/* ---------- tiny XML tag extractors (regex-style) ---------- */
/* find the first occurrence of <tag ...>value</tag> starting at *pp;
   returns pointer after the closing tag, or NULL if not found. */
static char *find_element(char **pp, const char *tag, char *out, int outsz)
{
    char *p = *pp;
    size_t tlen = strlen(tag);
    char open[64], close[64];
    char *start, *end;

    snprintf(open, sizeof(open), "<%s", tag);
    snprintf(close, sizeof(close), "</%s>", tag);

    start = strstr(p, open);
    if (!start) return NULL;
    end = strstr(start, close);
    if (!end) return NULL;

    /* value is between the end of the opening tag ('>') and the close tag */
    char *v = strchr(start, '>');
    if (!v || v > end) return NULL;
    v++;
    {
        size_t n = (size_t)(end - v);
        if (n >= (size_t)outsz) n = outsz - 1;
        memcpy(out, v, n);
        out[n] = '\0';
    }
    *pp = end + strlen(close);
    return out;
}

/* find device block <DeviceStream name="X">...</DeviceStream> */
static char *find_devicestream(char **pp, char *name, int namesz)
{
    char *p = strstr(*pp, "<DeviceStream ");
    if (!p) return NULL;
    char *n = strstr(p, "name=\"");
    if (!n) return NULL;
    n += 6;
    char *e = strchr(n, '"');
    if (!e) return NULL;
    size_t len = (size_t)(e - n);
    if (len >= (size_t)namesz) len = namesz - 1;
    memcpy(name, n, len);
    name[len] = '\0';

    char *close = strstr(p, "</DeviceStream>");
    if (!close) return NULL;
    *pp = close + strlen("</DeviceStream>");
    return p;
}

static long long parse_ll(const char *s)
{
    if (!s || !*s) return -1;
    return _strtoi64(s, NULL, 10);
}

/* parse the whole /current document into per-machine samples */
static int parse_current(const char *xml, MachineSample *out, int max, long long now)
{
    char *p = (char *)xml, *dev;
    char name[MAX_NAME];
    int n = 0;

    while (n < max && (dev = find_devicestream(&p, name, sizeof(name))) != NULL) {
        if (name[0] == '\0' || strncmp(name, "ZXJ", 3) != 0 && strncmp(name, "MZ", 2) != 0)
            continue;
        char *q = dev;
        MachineSample *s = &out[n];
        memset(s, 0, sizeof(*s));
        strncpy(s->name, name, sizeof(s->name) - 1);
        s->ts = now;
        s->valid = 1;

        char val[MAX_VALUE];
        if (find_element(&q, "Availability", val, sizeof(val)))
            if (strcmp(val, "AVAILABLE") != 0) s->valid = 0;
        if (find_element(&q, "Execution", val, sizeof(val)))
            strncpy(s->execution, val, sizeof(s->execution) - 1);
        if (find_element(&q, "ControllerMode", val, sizeof(val)))
            strncpy(s->mode, val, sizeof(s->mode) - 1);
        if (find_element(&q, "ProgramComment", val, sizeof(val)))
            strncpy(s->comment, val, sizeof(s->comment) - 1);
        if (find_element(&q, "Program", val, sizeof(val)))
            strncpy(s->program, val, sizeof(s->program) - 1);
        /* name="partTotal" -> Sample value */
        {
            char *sp = dev;
            while ((sp = strstr(sp, "name=\"partTotal\"")) != NULL) {
                char *gt = strchr(sp, '>');
                char *lt = gt ? strchr(gt, '<') : NULL;
                if (gt && lt) {
                    char tmp[64];
                    size_t nn = (size_t)(lt - gt - 1);
                    if (nn >= sizeof(tmp)) nn = sizeof(tmp) - 1;
                    memcpy(tmp, gt + 1, nn);
                    tmp[nn] = '\0';
                    s->part_total = parse_ll(tmp);
                    break;
                }
                sp += 15;
            }
            /* tmmode */
            sp = dev;
            if ((sp = strstr(sp, "name=\"tmMode\"")) != NULL) {
                char *gt = strchr(sp, '>');
                char *lt = gt ? strchr(gt, '<') : NULL;
                if (gt && lt) {
                    char tmp[32];
                    size_t nn = (size_t)(lt - gt - 1);
                    if (nn >= sizeof(tmp)) nn = sizeof(tmp) - 1;
                    memcpy(tmp, gt + 1, nn);
                    tmp[nn] = '\0';
                    strncpy(s->tmmode, tmp, sizeof(s->tmmode) - 1);
                }
            }
        }
        n++;
    }
    return n;
}
/* ---------- SQLite ---------- */
static sqlite3 *db_open(const char *path)
{
    sqlite3 *db = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        fprintf(stderr, "[mtc_stats] cannot open db %s: %s\n", path,
                sqlite3_errmsg(db));
        return NULL;
    }
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS samples("
        "  ts INTEGER NOT NULL,"
        "  machine TEXT NOT NULL,"
        "  execution TEXT, mode TEXT, tmmode TEXT,"
        "  program TEXT, comment TEXT,"
        "  part_total INTEGER,"
        "  PRIMARY KEY(ts, machine));"
        "CREATE INDEX IF NOT EXISTS idx_samples_machine ON samples(machine, ts);",
        NULL, NULL, NULL);
    return db;
}

static void db_insert(sqlite3 *db, const MachineSample *s)
{
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT OR REPLACE INTO samples(ts,machine,execution,mode,tmmode,"
        "program,comment,part_total) VALUES(?1,?2,?3,?4,?5,?6,?7,?8);";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return;
    sqlite3_bind_int64(st, 1, s->ts);
    sqlite3_bind_text(st, 2, s->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, s->execution, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, s->mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 5, s->tmmode, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 6, s->program, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 7, s->comment, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 8, s->part_total);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* ---------- poll loop ---------- */
static int cmd_poll(int port, int interval, const char *dbpath)
{
    sqlite3 *db = db_open(dbpath);
    if (!db) return 1;

    printf("[mtc_stats] polling http://127.0.0.1:%d/current every %ds -> %s\n",
           port, interval, dbpath);
    for (;;) {
        int len = 0;
        char *xml = http_get(port, "/current", &len);
        if (xml) {
            MachineSample samples[MAX_MACHINES];
            long long now = (long long)time(NULL);
            int n = parse_current(xml, samples, MAX_MACHINES, now);
            for (int i = 0; i < n; i++)
                db_insert(db, &samples[i]);
            printf("[%lld] captured %d machines\n", now, n);
            free(xml);
        } else {
            printf("[mtc_stats] agent not reachable, retrying in %ds\n", interval);
        }
        Sleep(interval * 1000);
    }
    sqlite3_close(db);
    return 0;
}
/* ---------- report ---------- */
typedef struct {
    char machine[MAX_NAME];
    char comment[MAX_VALUE];
    long long mach_sec;     /* machining seconds (ACTIVE+AUTO+tmmode=0) */
    long long start_total;  /* first part_total seen */
    long long end_total;    /* last part_total seen */
    long long first_ts, last_ts;
    int valid_total;
} ProductStat;

static int is_machining(const MachineSample *s)
{
    return strcmp(s->execution, "ACTIVE") == 0 &&
           strcmp(s->mode, "AUTOMATIC") == 0 &&
           strcmp(s->tmmode, "0") == 0;
}

static int cmd_report(const char *dbpath, int bucket_sec,
                      long long from, long long to)
{
    sqlite3 *db = db_open(dbpath);
    if (!db) return 1;

    /* fetch all samples in range, ordered by machine, ts */
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT ts,machine,execution,mode,tmmode,program,comment,part_total "
        "FROM samples WHERE ts>=?1 AND ts<=?2 ORDER BY machine, ts;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "[mtc_stats] prepare failed: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_int64(st, 1, from);
    sqlite3_bind_int64(st, 2, to);

    MachineSample *rows = (MachineSample *)calloc(65536, sizeof(MachineSample));
    int n = 0;
    if (!rows) { fprintf(stderr, "[mtc_stats] out of memory\n"); return 1; }
    while (sqlite3_step(st) == SQLITE_ROW && n < 65536) {
        MachineSample *r = &rows[n++];
        memset(r, 0, sizeof(*r));
        r->ts = sqlite3_column_int64(st, 0);
        strncpy(r->name, (const char *)sqlite3_column_text(st, 1), sizeof(r->name) - 1);
        strncpy(r->execution, (const char *)sqlite3_column_text(st, 2), sizeof(r->execution) - 1);
        strncpy(r->mode, (const char *)sqlite3_column_text(st, 3), sizeof(r->mode) - 1);
        strncpy(r->tmmode, (const char *)sqlite3_column_text(st, 4), sizeof(r->tmmode) - 1);
        strncpy(r->program, (const char *)sqlite3_column_text(st, 5), sizeof(r->program) - 1);
        strncpy(r->comment, (const char *)sqlite3_column_text(st, 6), sizeof(r->comment) - 1);
        r->part_total = sqlite3_column_int64(st, 7);
    }
    sqlite3_finalize(st);

    if (n == 0) {
        printf("[mtc_stats] no samples in range. Run 'mtc_stats poll' first.\n");
        free(rows);
        return 1;
    }

    char fbuf[32], tbuf[32];
    {
        struct tm t1, t2;
        time_t a = (time_t)from, b = (time_t)to;
        localtime_s(&t1, &a);
        localtime_s(&t2, &b);
        strftime(fbuf, sizeof(fbuf), "%Y-%m-%d %H:%M", &t1);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &t2);
    }
    printf("\n=== 加工统计  %s ~ %s  (bucket=%ds) ===\n", fbuf, tbuf, bucket_sec);

    /* (1) per-machine running time, bucketed */
    printf("\n[1] 机床实际运行时间 (ACTIVE + AUTOMATIC + tmmode=0, MachSec = count x interval)\n");
    printf("    %-7s %-16s %-10s\n", "Machine", "BucketStart", "MachSec");
    {
        long long bstart = from / bucket_sec * bucket_sec;
        long long bend = to / bucket_sec * bucket_sec;
        for (long long b = bstart; b <= bend; b += bucket_sec) {
            char bts[32];
            time_t bt = (time_t)b;
            struct tm bt2;
            localtime_s(&bt2, &bt);
            strftime(bts, sizeof(bts), "%m-%d %H:%M", &bt2);
            /* count machining samples in bucket per machine (sample interval
               approximates the machining seconds) */
            for (int m = 0; m < MAX_MACHINES; m++) {
                /* distinct machine set: iterate unique names */
            }
            /* collect unique machines first */
            static char machines[MAX_MACHINES][MAX_NAME];
            static int nm = 0;
            if (nm == 0) {
                for (int i = 0; i < n; i++) {
                    int found = 0;
                    for (int j = 0; j < nm; j++)
                        if (strcmp(machines[j], rows[i].name) == 0) { found = 1; break; }
                    if (!found && nm < MAX_MACHINES)
                        strncpy(machines[nm++], rows[i].name, MAX_NAME - 1);
                }
            }
            for (int j = 0; j < nm; j++) {
                long long cnt = 0;
                for (int i = 0; i < n; i++) {
                    if (strcmp(rows[i].name, machines[j]) != 0) continue;
                    if (rows[i].ts < b || rows[i].ts >= b + bucket_sec) continue;
                    if (is_machining(&rows[i])) cnt++;
                }
                if (cnt > 0)
                    printf("    %-7s %-16s %-10lld\n", machines[j], bts, cnt);
            }
        }
    }

    /* (2) per-machine produced parts in range (part_total delta) */
    printf("\n[2] 时间段内产量 (part_total #6712 差值)\n");
    printf("    %-7s %-14s %-14s %-10s\n", "Machine", "StartTotal", "EndTotal", "Produced");
    {
        static char machines[MAX_MACHINES][MAX_NAME];
        static int nm = 0;
        nm = 0;
        for (int i = 0; i < n; i++) {
            int found = 0;
            for (int j = 0; j < nm; j++)
                if (strcmp(machines[j], rows[i].name) == 0) { found = 1; break; }
            if (!found && nm < MAX_MACHINES)
                strncpy(machines[nm++], rows[i].name, MAX_NAME - 1);
        }
        for (int j = 0; j < nm; j++) {
            long long first = -1, last = -1;
            for (int i = 0; i < n; i++) {
                if (strcmp(rows[i].name, machines[j]) != 0) continue;
                if (rows[i].part_total < 0) continue;
                if (first < 0) first = rows[i].part_total;
                last = rows[i].part_total;
            }
            if (first >= 0 && last >= first)
                printf("    %-7s %-14lld %-14lld %-10lld\n",
                       machines[j], first, last, last - first);
        }
    }

    /* (3) per-machine per-product parts */
    printf("\n[3] 每种产品产量 (按程序注释分组)\n");
    printf("    %-7s %-22s %-12s\n", "Machine", "Product(comment)", "Produced");
    {
        static char machines[MAX_MACHINES][MAX_NAME];
        static int nm = 0;
        nm = 0;
        for (int i = 0; i < n; i++) {
            int found = 0;
            for (int j = 0; j < nm; j++)
                if (strcmp(machines[j], rows[i].name) == 0) { found = 1; break; }
            if (!found && nm < MAX_MACHINES)
                strncpy(machines[nm++], rows[i].name, MAX_NAME - 1);
        }
        for (int j = 0; j < nm; j++) {
            /* for each machine, walk samples in order; accumulate part_total
               delta while comment stays the same */
            ProductStat ps[MAX_MACHINES * 8];
            int np = 0;
            long long prev_total = -1;
            char prev_comment[MAX_VALUE] = "";
            ProductStat *cur = NULL;
            for (int i = 0; i < n; i++) {
                if (strcmp(rows[i].name, machines[j]) != 0) continue;
                if (rows[i].part_total < 0) continue;
                if (np == 0 || strcmp(rows[i].comment, prev_comment) != 0) {
                    if (np < MAX_MACHINES * 8) {
                        cur = &ps[np++];
                        memset(cur, 0, sizeof(*cur));
                        strncpy(cur->machine, machines[j], MAX_NAME - 1);
                        strncpy(cur->comment, rows[i].comment, MAX_VALUE - 1);
                        cur->start_total = -1;
                        cur->end_total = -1;
                        strncpy(prev_comment, rows[i].comment, MAX_VALUE - 1);
                    } else cur = NULL;
                }
                if (!cur) continue;
                if (cur->start_total < 0) cur->start_total = rows[i].part_total;
                cur->end_total = rows[i].part_total;
            }
            for (int k = 0; k < np; k++) {
                long long produced = (ps[k].end_total >= ps[k].start_total && ps[k].start_total >= 0)
                    ? ps[k].end_total - ps[k].start_total : 0;
                if (ps[k].comment[0] && produced > 0)
                    printf("    %-7s %-22s %-12lld\n",
                           machines[j], ps[k].comment, produced);
            }
        }
    }
    free(rows);

    sqlite3_close(db);
    return 0;
}
int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);  /* keep UTF-8 output readable on GBK consoles */
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s poll [http_port] [interval_sec] [db]\n", argv[0]);
        printf("      poll the agent and store samples (default port 5000, 5s, stats.db)\n");
        printf("  %s report [db] [bucket_sec] [from_unix] [to_unix]\n", argv[0]);
        printf("      running time / produced parts / per-product parts\n");
        printf("      (default last 24h, 30min buckets)\n");
        return 1;
    }

    if (strcmp(argv[1], "poll") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : 5000;
        int interval = argc > 3 ? atoi(argv[3]) : 5;
        const char *db = argc > 4 ? argv[4] : "stats.db";
        return cmd_poll(port, interval, db);
    }

    if (strcmp(argv[1], "report") == 0) {
        const char *db = argc > 2 ? argv[2] : "stats.db";
        int bucket = argc > 3 ? atoi(argv[3]) : 1800;
        long long from, to;
        time_t now = time(NULL);
        if (argc > 5) {
            from = _atoi64(argv[4]);
            to = _atoi64(argv[5]);
        } else {
            to = (long long)now;
            from = to - 24 * 3600;
        }
        return cmd_report(db, bucket, from, to);
    }

    printf("unknown command '%s'\n", argv[1]);
    return 1;
}
