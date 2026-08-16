/*
 * cnc_sim.c - Pure C CNC machine simulator (SHDR + auto cycle + HTTP control)
 *
 * Emits realistic FANUC-style MTConnect SHDR data on <shdr_port> and keeps
 * generating machine cycles automatically (auto_cycle=ON). Machine state can
 * additionally be controlled via a local HTTP API on <control_port>; issuing
 * any control command switches to manual takeover (`auto on` re-enables the
 * auto cycle).
 *
 *   GET  /                  -> command help
 *   GET  /state             -> machine state (JSON)
 *   POST /control           -> {"cmd":"start", ...}  (GET /control?cmd=... also works)
 *
 * Commands:
 *   start | stop | hold | resume | reset
 *   estop | estop_release
 *   mode <AUTOMATIC|MANUAL|MDI>
 *   program <O1000|O2000|O3000>
 *   （每个程序含多个产品名 programInfo，每加工 10 件自动切换下一个）
 *   alarm <none|spindle|servo|overtravel|overheat|comms|logic|motion|system>
 *   jog <axis> <dir> <dist>     (manual jog, axis X/Y/Z, dir +/-)
 *   mdi <axis> <dist>           (single MDI move)
 *   set <key> <value>           (Fovr, SspeedOvr, part_required,
 *                                part_total, part_current, spindle)
 *   setpos <axis> <value>       (force a position)
 *   auto <on|off>               (re-enable / disable auto cycle)
 *
 * Compile (Linux):
 *   gcc -std=c11 -Wall -Wextra -pthread -o cnc_sim cnc_sim.c -lm
 *
 * Compile (Windows / MSVC):
 *   cl /std:c11 /EHsc cnc_sim.c /link ws2_32.lib
 *
 * Usage:
 *   ./cnc_sim [shdr_port] [control_port] [interval_ms] [name]
 *     shdr_port    SHDR listen port (default 7878)
 *     control_port HTTP control port (default shdr_port + 2000)
 *     interval_ms  sample interval (default 500)
 *     name         machine name (default SIM01)
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===================== 常量 ===================== */

#define MAX_CLIENTS 32
#define RAPID_FEED 6000.0
#define MAX_BLOCKS 32
#define MAX_PROGRAMS 8
#define MAX_PRODUCTS 8
#define PRODUCT_SWITCH_PARTS 10 /* 每加工 10 件切换一个产品(programInfo) */
#define MAX_KV 24
#define MAX_HTTP 16384

#ifdef _WIN32
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
static void sock_close(sock_t s) { closesocket(s); }
#else
typedef int sock_t;
#define SOCK_BAD (-1)
static void sock_close(sock_t s) { close(s); }
#endif

/* ===================== 枚举 ===================== */

typedef enum
{
    EXEC_READY,
    EXEC_ACTIVE,
    EXEC_INTERRUPTED,
    EXEC_STOPPED
} ExecState;
typedef enum
{
    MODE_AUTO,
    MODE_MANUAL,
    MODE_MDI
} CtrlMode;
typedef enum
{
    BLK_RAPID,
    BLK_FEED,
    BLK_SPINDLE,
    BLK_END
} BlkType;
typedef enum
{
    ALARM_NONE,
    ALARM_SPINDLE,
    ALARM_SERVO,
    ALARM_OVERTRAVEL,
    ALARM_OVERHEAT,
    ALARM_COMMS,
    ALARM_LOGIC,
    ALARM_MOTION,
    ALARM_SYSTEM
} AlarmId;

/* ===================== 数据结构 ===================== */

typedef struct
{
    BlkType type;
    double x, y, z;
    double feed;
    double spindle;
    char text[64];
} Block;

typedef struct
{
    char id[16];
    char label[64];
    char products[MAX_PRODUCTS][64]; /* 产品名（程序注释），循环切换 */
    int nproducts;
    Block blocks[MAX_BLOCKS];
    int nblocks;
} Program;

/* ===================== 程序库 ===================== */

static Program g_programs[MAX_PROGRAMS];
static int g_nprog = 0;

static void set_products(Program *p, const char *const *names, int n)
{
    if (n > MAX_PRODUCTS)
        n = MAX_PRODUCTS;
    p->nproducts = n;
    for (int i = 0; i < n; i++)
        snprintf(p->products[i], sizeof(p->products[i]), "%s", names[i]);
}

static void init_programs(void)
{
    Program *p;
    static const char *const prods_o1000[] = {
        "c-12-bracket-001/A0", "c-12-bracket-002/A0", "c-12-bracket-003/A0",
        "c-12-bracket-004/A0", "c-12-bracket-005/A0", "c-12-bracket-006/A0",
    };
    static const char *const prods_o2000[] = {
        "c-12-shaft-001/A0", "c-12-shaft-002/A0", "c-12-shaft-003/A0",
        "c-12-shaft-004/A0", "c-12-shaft-005/A0", "c-12-shaft-006/A0",
    };
    static const char *const prods_o3000[] = {
        "c-12-finish-001/A0", "c-12-finish-002/A0", "c-12-finish-003/A0",
        "c-12-finish-004/A0", "c-12-finish-005/A0", "c-12-finish-006/A0",
    };

    /* --- O1000 --- */
    p = &g_programs[g_nprog++];
    snprintf(p->id, sizeof(p->id), "O1000");
    snprintf(p->label, sizeof(p->label), "Aluminum bracket");
    set_products(p, prods_o1000, (int)(sizeof(prods_o1000) / sizeof(prods_o1000[0])));
    p->nblocks = 0;
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 0, 0, 10, 0, 0, "G00 X0 Y0 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 50, 30, 10, 0, 0, "G00 X50 Y30 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_SPINDLE, 50, 30, 10, 0, 1500, "S1500 M03"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 50, 30, -5, 300, 1500, "G01 Z-5 F300"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 100, 30, -5, 600, 1500, "G01 X100 Y30 F600"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 100, 70, -5, 600, 1500, "G01 X100 Y70 F600"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 50, 50, -5, 500, 1500, "G01 X50 Y50 F500"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 50, 30, -5, 400, 1500, "G01 X50 Y30 F400"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 50, 30, 10, 300, 1500, "G01 Z10 F300"};
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 0, 0, 10, 0, 0, "G00 X0 Y0 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_SPINDLE, 0, 0, 10, 0, 0, "M05"};
    p->blocks[p->nblocks++] = (Block){BLK_END, 0, 0, 10, 0, 0, "M30"};

    /* --- O2000 --- */
    p = &g_programs[g_nprog++];
    snprintf(p->id, sizeof(p->id), "O2000");
    snprintf(p->label, sizeof(p->label), "Steel shaft");
    set_products(p, prods_o2000, (int)(sizeof(prods_o2000) / sizeof(prods_o2000[0])));
    p->nblocks = 0;
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 0, 0, 10, 0, 0, "G00 X0 Y0 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 30, 20, 10, 0, 0, "G00 X30 Y20 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_SPINDLE, 30, 20, 10, 0, 800, "S800 M03"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 30, 20, -3, 150, 800, "G01 Z-3 F150"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 80, 20, -3, 200, 800, "G01 X80 Y20 F200"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 80, 40, -3, 200, 800, "G01 X80 Y40 F200"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 30, 20, -3, 150, 800, "G01 X30 Y20 F150"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 30, 20, 10, 200, 800, "G01 Z10 F200"};
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 0, 0, 10, 0, 0, "G00 X0 Y0 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_SPINDLE, 0, 0, 10, 0, 0, "M05"};
    p->blocks[p->nblocks++] = (Block){BLK_END, 0, 0, 10, 0, 0, "M30"};

    /* --- O3000 --- */
    p = &g_programs[g_nprog++];
    snprintf(p->id, sizeof(p->id), "O3000");
    snprintf(p->label, sizeof(p->label), "Finish Pass");
    set_products(p, prods_o3000, (int)(sizeof(prods_o3000) / sizeof(prods_o3000[0])));
    p->nblocks = 0;
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 0, 0, 10, 0, 0, "G00 X0 Y0 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 10, 10, 10, 0, 0, "G00 X10 Y10 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_SPINDLE, 10, 10, 10, 0, 2000, "S2000 M03"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 10, 10, -2, 200, 2000, "G01 Z-2 F200"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 90, 10, -2, 400, 2000, "G01 X90 Y10 F400"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 90, 60, -2, 400, 2000, "G01 X90 Y60 F400"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 10, 10, -2, 350, 2000, "G01 X10 Y10 F350"};
    p->blocks[p->nblocks++] = (Block){BLK_FEED, 10, 10, 10, 250, 2000, "G01 Z10 F250"};
    p->blocks[p->nblocks++] = (Block){BLK_RAPID, 0, 0, 10, 0, 0, "G00 X0 Y0 Z10"};
    p->blocks[p->nblocks++] = (Block){BLK_SPINDLE, 0, 0, 10, 0, 0, "M05"};
    p->blocks[p->nblocks++] = (Block){BLK_END, 0, 0, 10, 0, 0, "M30"};
}

/* ===================== 机床状态 ===================== */

typedef struct
{
    char name[64];
    bool avail;
    bool estop;
    bool auto_mode; /* true = auto cycle keeps generating data */
    ExecState exec;
    CtrlMode mode;
    AlarmId alarm;
    int prog_index;
    int product_index; /* 当前产品（programInfo）在 program.products[] 中的下标 */
    int line;
    double frac;
    double sx, sy, sz;
    double tx, ty, tz;
    double x, y, z;
    double feed_act;
    int feed_ovr;
    double sp_act, sp_tgt;
    int sp_ovr;
    double sp_load;
    double lx, ly, lz;
    int part, part_current, part_required, part_total;
    double probe_x, probe_y, probe_z;
    bool jog_active;
    char jog_axis;
    double jog_target, jog_remaining, jog_feed;
    bool mdi_active;
    char mdi_axis;
    double mdi_target, mdi_remaining, mdi_feed, mdi_sp;
    uint64_t state_t0_ms;
    uint64_t boot_ms;
} Machine;

static Machine g_m;

/* 当前产品名（programInfo）；每加工 PRODUCT_SWITCH_PARTS 件切换下一个 */
static const char *current_product(void)
{
    const Program *p = &g_programs[g_m.prog_index];
    if (p->nproducts <= 0)
        return p->label;
    if (g_m.product_index < 0 || g_m.product_index >= p->nproducts)
        g_m.product_index = 0;
    return p->products[g_m.product_index];
}

/* 由已完成件数推导产品下标：第 1..10 件 -> 产品0，11..20 件 -> 产品1，循环 */
static void update_product_index(void)
{
    const Program *p = &g_programs[g_m.prog_index];
    if (p->nproducts <= 0)
    {
        g_m.product_index = 0;
        return;
    }
    int idx = (g_m.part_current - 1) / PRODUCT_SWITCH_PARTS;
    idx = idx % p->nproducts;
    if (idx < 0)
        idx = 0;
    g_m.product_index = idx;
}

/* ---------------- 锁（控制线程 / 主循环共享状态） ---------------- */

#ifdef _WIN32
static CRITICAL_SECTION g_lock;
static void lock_init(void) { InitializeCriticalSection(&g_lock); }
static void lock(void) { EnterCriticalSection(&g_lock); }
static void unlock(void) { LeaveCriticalSection(&g_lock); }
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock_init(void) {}
static void lock(void) { pthread_mutex_lock(&g_lock); }
static void unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

/* ===================== 客户端管理 ===================== */

#ifdef _WIN32
static SOCKET g_clients[MAX_CLIENTS];
#else
static int g_clients[MAX_CLIENTS];
static pthread_mutex_t g_cli_mtx = PTHREAD_MUTEX_INITIALIZER;
#endif

static void clients_init(void)
{
#ifdef _WIN32
    for (int i = 0; i < MAX_CLIENTS; i++)
        g_clients[i] = INVALID_SOCKET;
#else
    for (int i = 0; i < MAX_CLIENTS; i++)
        g_clients[i] = -1;
#endif
}

static void clients_add(
#ifdef _WIN32
    SOCKET c
#else
    int c
#endif
)
{
#ifdef _WIN32
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == INVALID_SOCKET)
        {
            g_clients[i] = c;
            return;
        }
#else
    pthread_mutex_lock(&g_cli_mtx);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == -1)
        {
            g_clients[i] = c;
            pthread_mutex_unlock(&g_cli_mtx);
            return;
        }
    pthread_mutex_unlock(&g_cli_mtx);
#endif
}

/* ===================== 时间工具 ===================== */

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
#endif
}

static void utc_timestamp(char *buf, size_t len)
{
#ifdef _WIN32
    SYSTEMTIME st;
    GetSystemTime(&st);
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    struct timeval tv;
    struct tm tm;
    gettimeofday(&tv, NULL);
    time_t t = (time_t)tv.tv_sec;
    gmtime_r(&t, &tm);
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));
#endif
}

static double jitter(void)
{
    return ((double)(rand() % 1000) / 1000.0 - 0.5) * 0.004;
}

/* ===================== SHDR 发送 ===================== */

static void send_line_all(const char *line)
{
    char buf[600];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n <= 0)
        return;
#ifdef _WIN32
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] != INVALID_SOCKET)
            send(g_clients[i], buf, n, 0);
#else
    pthread_mutex_lock(&g_cli_mtx);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] >= 0)
            send(g_clients[i], buf, n, 0);
    pthread_mutex_unlock(&g_cli_mtx);
#endif
}

static const char *exec_str(ExecState e)
{
    switch (e)
    {
    case EXEC_ACTIVE:
        return "ACTIVE";
    case EXEC_INTERRUPTED:
        return "INTERRUPTED";
    case EXEC_STOPPED:
        return "STOPPED";
    default:
        return "READY";
    }
}

static const char *mode_str(CtrlMode m)
{
    switch (m)
    {
    case MODE_MANUAL:
        return "MANUAL";
    case MODE_MDI:
        return "MANUAL_DATA_INPUT";
    default:
        return "AUTOMATIC";
    }
}

/* ---------------- 报警定义 / 条件输出 ---------------- */

typedef struct
{
    const char *name;
    const char *code;
    const char *msg;
} AlarmDef;

static const AlarmDef g_alarms[] = {
    {"none",        "",            ""},
    {"spindle",     "SPINDLE-OVL", "SPINDLE OVERLOAD"},
    {"servo",       "SERVO-ALM",   "SERVO ALARM"},
    {"overtravel",  "OT+X",        "OVERTRAVEL +X"},
    {"overheat",    "OVH-X",       "AXIS OVERHEAT X"},
    {"comms",       "COMMS-ERR",   "COMMUNICATION ERROR"},
    {"logic",       "LOGIC-ALM",   "LOGIC ALARM"},
    {"motion",      "MOTION-ALM",  "MOTION ALARM"},
    {"system",      "SYSTEM-ALM",  "SYSTEM ALARM"},
};
#define N_ALARMS ((int)(sizeof(g_alarms) / sizeof(g_alarms[0])))

static const char *g_cond_ids[] = {
    "servo", "comms", "logic", "motion", "system", "spindle",
    "Xtravel", "Xovertemp", "Xservo",
    "Ytravel", "Yovertemp", "Yservo",
    "Ztravel", "Zovertemp", "Zservo"
};
#define N_COND_IDS ((int)(sizeof(g_cond_ids) / sizeof(g_cond_ids[0])))

static bool cond_fault(const char *id)
{
    switch (g_m.alarm)
    {
    case ALARM_SPINDLE:
        return strcmp(id, "spindle") == 0;
    case ALARM_SERVO:
        return strcmp(id, "servo") == 0 ||
               strcmp(id, "Xservo") == 0 ||
               strcmp(id, "Yservo") == 0 ||
               strcmp(id, "Zservo") == 0;
    case ALARM_OVERTRAVEL:
        return strcmp(id, "Xtravel") == 0;
    case ALARM_OVERHEAT:
        return strcmp(id, "Xovertemp") == 0;
    case ALARM_COMMS:
        return strcmp(id, "comms") == 0;
    case ALARM_LOGIC:
        return strcmp(id, "logic") == 0;
    case ALARM_MOTION:
        return strcmp(id, "motion") == 0;
    case ALARM_SYSTEM:
        return strcmp(id, "system") == 0;
    default:
        return false;
    }
}

static void send_cond(const char *ts, const char *id)
{
    char line[256];
    if (g_m.alarm != ALARM_NONE && cond_fault(id))
    {
        const AlarmDef *a = &g_alarms[g_m.alarm];
        snprintf(line, sizeof(line), "%s|%s|FAULT|%s|%s|", ts, id, a->code, a->msg);
    }
    else
    {
        snprintf(line, sizeof(line), "%s|%s|NORMAL|||", ts, id);
    }
    send_line_all(line);
}

static void emit_state(void)
{
    char ts[64];
    char line[512];
    utc_timestamp(ts, sizeof(ts));

    const Program *prog = &g_programs[g_m.prog_index];
    const Block *blk = (g_m.exec == EXEC_ACTIVE && g_m.line >= 0 && g_m.line < prog->nblocks)
                           ? &prog->blocks[g_m.line]
                           : NULL;

#define PUT(...)                                   \
    do                                             \
    {                                              \
        snprintf(line, sizeof(line), __VA_ARGS__); \
        send_line_all(line);                       \
    } while (0)

    PUT("%s|avail|%s", ts, g_m.avail ? "AVAILABLE" : "UNAVAILABLE");
    PUT("%s|estop|%s", ts, g_m.estop ? "TRIGGERED" : "ARMED");
    PUT("%s|execution|%s", ts, exec_str(g_m.exec));
    PUT("%s|mode|%s", ts, mode_str(g_m.mode));
    PUT("%s|program|%s", ts, prog->id);
    PUT("%s|programInfo|%s", ts, current_product());
    PUT("%s|block|%s", ts, blk ? blk->text : "");
    PUT("%s|line|%d", ts, blk ? g_m.line + 1 : 0);
    PUT("%s|pathFeedrate|%.1f", ts, g_m.feed_act);
    PUT("%s|pathPosition|%.3f %.3f %.3f", ts, g_m.x, g_m.y, g_m.z);
    PUT("%s|part|%d", ts, g_m.part_current);
    PUT("%s|part_current|%d", ts, g_m.part_current);
    PUT("%s|part_required|%d", ts, g_m.part_required);
    PUT("%s|part_total|%d", ts, g_m.part_total);
    PUT("%s|tmmode|0", ts);
    PUT("%s|Xact|%.3f", ts, g_m.x + jitter());
    PUT("%s|Xcom|%.3f", ts, g_m.tx);
    PUT("%s|Xload|%.1f", ts, g_m.lx);
    PUT("%s|Yact|%.3f", ts, g_m.y + jitter());
    PUT("%s|Ycom|%.3f", ts, g_m.ty);
    PUT("%s|Yload|%.1f", ts, g_m.ly);
    PUT("%s|Zact|%.3f", ts, g_m.z + jitter());
    PUT("%s|Zcom|%.3f", ts, g_m.tz);
    PUT("%s|Zload|%.1f", ts, g_m.lz);
    PUT("%s|Sspeed|%.1f", ts, g_m.sp_act);
    PUT("%s|Sload|%.1f", ts, g_m.sp_load);
    PUT("%s|Fovr|%d", ts, g_m.feed_ovr);
    PUT("%s|SspeedOvr|%d", ts, g_m.sp_ovr);
    PUT("%s|probe|%.3f %.3f %.3f", ts, g_m.probe_x, g_m.probe_y, g_m.probe_z);
    if (g_m.alarm != ALARM_NONE)
        PUT("%s|message|%s", ts, g_alarms[g_m.alarm].msg);
    for (int i = 0; i < N_COND_IDS; i++)
        send_cond(ts, g_cond_ids[i]);

#undef PUT
}

/* ===================== 仿真逻辑 ===================== */

static double axis_get(char ax)
{
    if (ax == 'X') return g_m.x;
    if (ax == 'Y') return g_m.y;
    return g_m.z;
}

static void axis_set(char ax, double v)
{
    if (ax == 'X') g_m.x = v;
    else if (ax == 'Y') g_m.y = v;
    else g_m.z = v;
}

static void enter_block(int idx)
{
    const Program *p = &g_programs[g_m.prog_index];
    if (idx < 0 || idx >= p->nblocks)
        return;
    const Block *b = &p->blocks[idx];
    g_m.line = idx;
    g_m.frac = 0.0;
    g_m.sx = g_m.x;
    g_m.sy = g_m.y;
    g_m.sz = g_m.z;
    g_m.tx = b->x;
    g_m.ty = b->y;
    g_m.tz = b->z;
    if (b->type == BLK_SPINDLE)
        g_m.sp_tgt = b->spindle;
}

static void sim_step(double dt)
{
    const Program *prog = &g_programs[g_m.prog_index];

    /* spindle ramp */
    if (g_m.sp_act < g_m.sp_tgt)
    {
        g_m.sp_act += 2500.0 * dt;
        if (g_m.sp_act > g_m.sp_tgt)
            g_m.sp_act = g_m.sp_tgt;
    }
    else if (g_m.sp_act > g_m.sp_tgt)
    {
        g_m.sp_act -= 2500.0 * dt;
        if (g_m.sp_act < g_m.sp_tgt)
            g_m.sp_act = g_m.sp_tgt;
    }

    if (g_m.exec != EXEC_ACTIVE || g_m.estop)
    {
        if (g_m.jog_active && !g_m.estop)
        {
            double axis = axis_get(g_m.jog_axis);
            double move = g_m.jog_feed * g_m.feed_ovr / 100.0 / 60.0 * dt;
            if (move > g_m.jog_remaining)
                move = g_m.jog_remaining;
            if (g_m.jog_target > axis)
                axis += move;
            else
                axis -= move;
            axis_set(g_m.jog_axis, axis);
            g_m.jog_remaining -= move;
            g_m.feed_act = g_m.jog_feed * g_m.feed_ovr / 100.0;
            if (g_m.jog_remaining <= 0.0)
            {
                g_m.jog_active = false;
                g_m.jog_remaining = 0.0;
                g_m.feed_act = 0.0;
            }
            g_m.lx = 4.0 + (rand() % 6);
            g_m.ly = 3.0 + (rand() % 5);
            g_m.lz = 5.0 + (rand() % 6);
        }
        else
        {
            g_m.feed_act = 0.0;
            g_m.lx = 1.0 + (rand() % 3);
            g_m.ly = 1.0 + (rand() % 3);
            g_m.lz = 2.0 + (rand() % 4);
        }
        g_m.sp_load = (g_m.sp_act > 10.0) ? 8.0 + (rand() % 8) : 0.0;
        return;
    }

    /* MDI 单段移动 */
    if (g_m.mdi_active)
    {
        double axis = axis_get(g_m.mdi_axis);
        double move = g_m.mdi_feed * g_m.feed_ovr / 100.0 / 60.0 * dt;
        if (move > g_m.mdi_remaining)
            move = g_m.mdi_remaining;
        if (g_m.mdi_target > axis)
            axis += move;
        else
            axis -= move;
        axis_set(g_m.mdi_axis, axis);
        g_m.mdi_remaining -= move;
        g_m.feed_act = g_m.mdi_feed * g_m.feed_ovr / 100.0;
        if (g_m.mdi_remaining <= 0.0)
        {
            g_m.mdi_active = false;
            g_m.exec = EXEC_READY;
            g_m.feed_act = 0.0;
            g_m.sp_tgt = 0.0;
        }
        g_m.lx = 3.0 + (rand() % 5);
        g_m.ly = 3.0 + (rand() % 5);
        g_m.lz = 4.0 + (rand() % 6);
        g_m.sp_load = (g_m.sp_act > 10.0) ? 12.0 + (rand() % 8) : 0.0;
        return;
    }

    const Block *b = &prog->blocks[g_m.line];

    if (b->type == BLK_END)
    {
        /* 加工结束 -> 计数 +1 -> 重置 */
        g_m.part++;
        g_m.part_current++;
        g_m.part_total++;
        update_product_index(); /* 每 10 件切换一个产品 */
        g_m.exec = EXEC_READY;
        g_m.line = 0;
        g_m.frac = 0.0;
        g_m.x = g_m.y = 0.0;
        g_m.z = 10.0;
        g_m.probe_x = g_m.x;
        g_m.probe_y = g_m.y;
        g_m.probe_z = g_m.z;
        g_m.feed_act = 0.0;
        g_m.sp_tgt = 0.0;
        return;
    }

    double feed = ((int)b->type == BLK_RAPID ? RAPID_FEED : b->feed) * g_m.feed_ovr / 100.0;
    g_m.feed_act = feed;

    double dx = b->x - g_m.sx, dy = b->y - g_m.sy, dz = b->z - g_m.sz;
    double len = sqrt(dx * dx + dy * dy + dz * dz);

    if (len > 1e-9)
    {
        double dist = feed / 60.0 * dt;
        g_m.frac += dist / len;
        if (g_m.frac >= 1.0)
        {
            g_m.x = b->x;
            g_m.y = b->y;
            g_m.z = b->z;
            enter_block(g_m.line + 1);
        }
        else
        {
            g_m.x = g_m.sx + dx * g_m.frac;
            g_m.y = g_m.sy + dy * g_m.frac;
            g_m.z = g_m.sz + dz * g_m.frac;
        }
    }
    else
    {
        enter_block(g_m.line + 1);
    }

    /* 轴负载 */
    bool cutting = ((int)b->type == BLK_FEED && g_m.z < 0.0 && g_m.sp_act > 100.0);
    if (cutting)
    {
        g_m.lx = 35.0 + (rand() % 28);
        g_m.ly = 25.0 + (rand() % 25);
        g_m.lz = 18.0 + (rand() % 20);
    }
    else
    {
        g_m.lx = 2.0 + (rand() % 4);
        g_m.ly = 2.0 + (rand() % 4);
        g_m.lz = 3.0 + (rand() % 5);
    }

    g_m.sp_load = (g_m.sp_act > 10.0)
                      ? (cutting ? 55.0 + (rand() % 30) : 10.0 + (rand() % 12))
                      : 0.0;
}

/* ===================== 自动循环状态机 ===================== */

static void auto_cycle_tick(void)
{
    /* manual takeover (HTTP control) or estop pauses the auto cycle */
    if (!g_m.auto_mode || g_m.estop)
        return;

    uint64_t now = now_ms();
    uint64_t elapsed = now - g_m.state_t0_ms;

    switch (g_m.exec)
    {
    case EXEC_READY:
        if (elapsed > 2000)
        {
            g_m.exec = EXEC_ACTIVE;
            enter_block(0);
            g_m.state_t0_ms = now;
        }
        break;
    case EXEC_ACTIVE:
        if (elapsed > 10000)
        {
            g_m.exec = EXEC_INTERRUPTED;
            g_m.feed_act = 0.0;
            g_m.state_t0_ms = now;
        }
        break;
    case EXEC_INTERRUPTED:
        if (elapsed > 3000)
        {
            g_m.exec = EXEC_ACTIVE;
            g_m.state_t0_ms = now;
        }
        break;
    case EXEC_STOPPED:
        if (elapsed > 3000)
        {
            g_m.exec = EXEC_READY;
            g_m.line = 0;
            g_m.x = g_m.y = 0.0;
            g_m.z = 10.0;
            g_m.state_t0_ms = now;
        }
        break;
    }
}

/* ===================== HTTP 控制 API ===================== */

typedef struct
{
    char key[64];
    char val[256];
} KV;

static int ieq(const char *a, const char *b)
{
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcasecmp(a, b) == 0;
#endif
}

static const char *kv_get(const KV *kvs, int n, const char *key)
{
    for (int i = 0; i < n; i++)
        if (ieq(kvs[i].key, key))
            return kvs[i].val;
    return NULL;
}

static void kv_add(KV *kvs, int *n, const char *key, const char *val)
{
    if (*n >= MAX_KV)
        return;
    strncpy(kvs[*n].key, key, sizeof(kvs[*n].key) - 1);
    kvs[*n].key[sizeof(kvs[*n].key) - 1] = '\0';
    strncpy(kvs[*n].val, val, sizeof(kvs[*n].val) - 1);
    kvs[*n].val[sizeof(kvs[*n].val) - 1] = '\0';
    (*n)++;
}

static int parse_query(const char *q, KV *kvs, int max)
{
    int n = 0;
    const char *p = q;
    while (*p && n < max)
    {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
        char seg[512];
        if (seglen >= sizeof(seg))
            seglen = sizeof(seg) - 1;
        memcpy(seg, p, seglen);
        seg[seglen] = '\0';
        char *eq = strchr(seg, '=');
        if (eq)
        {
            *eq = '\0';
            kv_add(kvs, &n, seg, eq + 1);
        }
        if (!amp)
            break;
        p = amp + 1;
    }
    return n;
}

/* 极简 JSON 解析：提取 "key":"value" / "key":数字 键值对 */
static int parse_json(const char *body, KV *kvs, int max)
{
    int n = 0;
    const char *p = body;
    while (*p && n < max)
    {
        const char *q = strchr(p, '"');
        if (!q)
            break;
        q++;
        const char *ke = strchr(q, '"');
        if (!ke)
            break;
        char key[64];
        size_t kl = (size_t)(ke - q);
        if (kl >= sizeof(key))
            kl = sizeof(key) - 1;
        memcpy(key, q, kl);
        key[kl] = '\0';
        p = ke + 1;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p != ':')
            continue;
        p++;
        while (*p == ' ' || *p == '\t')
            p++;
        char val[256];
        if (*p == '"')
        {
            p++;
            const char *ve = strchr(p, '"');
            if (!ve)
                break;
            size_t vl = (size_t)(ve - p);
            if (vl >= sizeof(val))
                vl = sizeof(val) - 1;
            memcpy(val, p, vl);
            val[vl] = '\0';
            p = ve + 1;
        }
        else
        {
            size_t vl = 0;
            while (*p && *p != ',' && *p != '}' && *p != '\r' && *p != '\n' &&
                   vl < sizeof(val) - 1)
                val[vl++] = *p++;
            val[vl] = '\0';
        }
        char *vs = val;
        while (*vs == ' ' || *vs == '\t')
            vs++;
        char *ve2 = vs + strlen(vs);
        while (ve2 > vs && (ve2[-1] == ' ' || ve2[-1] == '\t'))
            *--ve2 = '\0';
        kv_add(kvs, &n, key, vs);
    }
    return n;
}

static void state_json(char *buf, int cap)
{
    const Program *p = &g_programs[g_m.prog_index];
    const char *blk = "";
    int line = 0;
    if (g_m.exec == EXEC_ACTIVE && g_m.line >= 0 && g_m.line < p->nblocks)
    {
        blk = p->blocks[g_m.line].text;
        line = g_m.line + 1;
    }
    int until = PRODUCT_SWITCH_PARTS - (g_m.part_current % PRODUCT_SWITCH_PARTS);
    if (until == PRODUCT_SWITCH_PARTS)
        until = PRODUCT_SWITCH_PARTS;
    snprintf(buf, cap,
             "{\"name\":\"%s\",\"avail\":\"%s\",\"estop\":\"%s\",\"auto\":%s,"
             "\"execution\":\"%s\",\"mode\":\"%s\","
             "\"program\":\"%s\",\"programInfo\":\"%s\",\"product_index\":%d,"
             "\"parts_until_switch\":%d,\"line\":%d,\"block\":\"%s\","
             "\"feed_act\":%.1f,\"feed_ovr\":%d,"
             "\"spindle\":%.1f,\"spindle_ovr\":%d,\"spindle_load\":%.1f,"
             "\"position\":[%.3f,%.3f,%.3f],\"load\":[%.1f,%.1f,%.1f],"
             "\"part\":%d,\"part_current\":%d,\"part_required\":%d,\"part_total\":%d,"
             "\"tmmode\":0,\"alarm\":\"%s\",\"message\":\"%s\",\"uptime_s\":%llu}",
             g_m.name,
             g_m.avail ? "AVAILABLE" : "UNAVAILABLE",
             g_m.estop ? "TRIGGERED" : "ARMED",
             g_m.auto_mode ? "true" : "false",
             exec_str(g_m.exec), mode_str(g_m.mode),
             p->id, current_product(), g_m.product_index, until, line, blk,
             g_m.feed_act, g_m.feed_ovr,
             g_m.sp_act, g_m.sp_ovr, g_m.sp_load,
             g_m.x, g_m.y, g_m.z, g_m.lx, g_m.ly, g_m.lz,
             g_m.part, g_m.part_current, g_m.part_required, g_m.part_total,
             g_alarms[g_m.alarm].name,
             g_m.alarm != ALARM_NONE ? g_alarms[g_m.alarm].msg : "",
             (unsigned long long)((now_ms() - g_m.boot_ms) / 1000));
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* 返回 1 成功 / 0 失败（信息写入 msg） */
static int cmd_apply(const KV *kvs, int n, char *msg, int msgcap)
{
    const char *cmd = kv_get(kvs, n, "cmd");
    if (!cmd)
    {
        snprintf(msg, msgcap, "missing 'cmd'");
        return 0;
    }

    if (ieq(cmd, "state") || ieq(cmd, "status"))
    {
        snprintf(msg, msgcap, "ok");
        return 1;
    }
    if (ieq(cmd, "help"))
    {
        snprintf(msg, msgcap, "see GET / for the command list");
        return 1;
    }
    if (ieq(cmd, "auto"))
    {
        const char *v = kv_get(kvs, n, "auto");
        if (!v)
        {
            snprintf(msg, msgcap, "auto needs on|off");
            return 0;
        }
        g_m.auto_mode = (ieq(v, "on") || ieq(v, "1") || ieq(v, "true"));
        snprintf(msg, msgcap, "auto_cycle=%s", g_m.auto_mode ? "ON" : "OFF");
        return 1;
    }

    /* 其余命令都视为手动接管，暂停自动循环 */
    g_m.auto_mode = false;

    if (ieq(cmd, "start"))
    {
        if (g_m.estop)
        {
            snprintf(msg, msgcap, "emergency stop active - release estop first");
            return 0;
        }
        g_m.alarm = ALARM_NONE;
        g_m.mode = MODE_AUTO;
        g_m.exec = EXEC_ACTIVE;
        g_m.mdi_active = false;
        g_m.jog_active = false;
        g_m.x = 0.0;
        g_m.y = 0.0;
        g_m.z = 10.0;
        enter_block(0);
        g_m.sp_tgt = 0.0;
        snprintf(msg, msgcap, "program %s started", g_programs[g_m.prog_index].id);
        return 1;
    }
    if (ieq(cmd, "stop"))
    {
        if (g_m.exec == EXEC_ACTIVE || g_m.exec == EXEC_INTERRUPTED)
            g_m.exec = EXEC_STOPPED;
        g_m.feed_act = 0.0;
        snprintf(msg, msgcap, "stopped at line %d", g_m.line + 1);
        return 1;
    }
    if (ieq(cmd, "hold"))
    {
        if (g_m.exec == EXEC_ACTIVE)
            g_m.exec = EXEC_INTERRUPTED;
        g_m.feed_act = 0.0;
        snprintf(msg, msgcap, "hold");
        return 1;
    }
    if (ieq(cmd, "resume"))
    {
        if (g_m.estop)
        {
            snprintf(msg, msgcap, "emergency stop active - release estop first");
            return 0;
        }
        if ((g_m.exec == EXEC_STOPPED || g_m.exec == EXEC_INTERRUPTED) &&
            (g_m.mode == MODE_AUTO || g_m.mdi_active))
            g_m.exec = EXEC_ACTIVE;
        snprintf(msg, msgcap, "resumed");
        return 1;
    }
    if (ieq(cmd, "reset"))
    {
        g_m.exec = EXEC_READY;
        g_m.line = 0;
        g_m.frac = 0.0;
        g_m.x = 0.0;
        g_m.y = 0.0;
        g_m.z = 10.0;
        g_m.sx = g_m.tx = 0.0;
        g_m.sy = g_m.ty = 0.0;
        g_m.sz = g_m.tz = 10.0;
        g_m.sp_tgt = 0.0;
        g_m.feed_act = 0.0;
        g_m.alarm = ALARM_NONE;
        g_m.jog_active = false;
        g_m.mdi_active = false;
        g_m.part_current = 0;
        g_m.product_index = 0;
        snprintf(msg, msgcap, "reset to program start");
        return 1;
    }
    if (ieq(cmd, "estop"))
    {
        g_m.estop = true;
        g_m.exec = EXEC_STOPPED;
        g_m.sp_tgt = 0.0;
        g_m.feed_act = 0.0;
        snprintf(msg, msgcap, "emergency stop engaged");
        return 1;
    }
    if (ieq(cmd, "estop_release"))
    {
        g_m.estop = false;
        g_m.exec = EXEC_READY;
        snprintf(msg, msgcap, "emergency stop released");
        return 1;
    }
    if (ieq(cmd, "mode"))
    {
        const char *v = kv_get(kvs, n, "mode");
        if (!v)
        {
            snprintf(msg, msgcap, "mode value required (AUTOMATIC|MANUAL|MDI)");
            return 0;
        }
        if (ieq(v, "AUTO") || ieq(v, "AUTOMATIC"))
            g_m.mode = MODE_AUTO;
        else if (ieq(v, "MANUAL") || ieq(v, "JOG"))
            g_m.mode = MODE_MANUAL;
        else if (ieq(v, "MDI") || ieq(v, "MANUAL_DATA_INPUT"))
            g_m.mode = MODE_MDI;
        else
        {
            snprintf(msg, msgcap, "unknown mode '%s'", v);
            return 0;
        }
        g_m.exec = EXEC_READY;
        g_m.sp_tgt = 0.0;
        g_m.feed_act = 0.0;
        snprintf(msg, msgcap, "mode set to %s", mode_str(g_m.mode));
        return 1;
    }
    if (ieq(cmd, "program"))
    {
        const char *v = kv_get(kvs, n, "program");
        if (!v)
        {
            snprintf(msg, msgcap, "program value required");
            return 0;
        }
        for (int i = 0; i < g_nprog; i++)
        {
            if (ieq(v, g_programs[i].id) || ieq(v, g_programs[i].id + 1))
            {
                g_m.prog_index = i;
                g_m.exec = EXEC_READY;
                g_m.line = 0;
                g_m.frac = 0.0;
                g_m.x = 0.0;
                g_m.y = 0.0;
                g_m.z = 10.0;
                g_m.sx = g_m.tx = 0.0;
                g_m.sy = g_m.ty = 0.0;
                g_m.sz = g_m.tz = 10.0;
                g_m.part_current = 0;
                g_m.product_index = 0;
                g_m.sp_tgt = 0.0;
                g_m.feed_act = 0.0;
                snprintf(msg, msgcap, "program set to %s (%d products)",
                         g_programs[i].id, g_programs[i].nproducts);
                return 1;
            }
        }
        snprintf(msg, msgcap, "unknown program (O1000|O2000|O3000)");
        return 0;
    }
    if (ieq(cmd, "alarm"))
    {
        const char *v = kv_get(kvs, n, "alarm");
        if (!v)
        {
            snprintf(msg, msgcap, "alarm value required");
            return 0;
        }
        for (int i = 0; i < N_ALARMS; i++)
        {
            if (ieq(v, g_alarms[i].name))
            {
                g_m.alarm = (AlarmId)i;
                if (g_m.alarm != ALARM_NONE && g_m.exec == EXEC_ACTIVE)
                    g_m.exec = EXEC_INTERRUPTED;
                snprintf(msg, msgcap, "alarm set: %s", g_alarms[i].name);
                return 1;
            }
        }
        snprintf(msg, msgcap,
                 "unknown alarm (none|spindle|servo|overtravel|overheat|"
                 "comms|logic|motion|system)");
        return 0;
    }
    if (ieq(cmd, "jog"))
    {
        const char *ax = kv_get(kvs, n, "axis");
        const char *dir = kv_get(kvs, n, "dir");
        const char *ds = kv_get(kvs, n, "dist");
        if (!ax || !dir || !ds)
        {
            snprintf(msg, msgcap, "jog needs axis, dir (+/-) and dist");
            return 0;
        }
        if (g_m.exec == EXEC_ACTIVE)
        {
            snprintf(msg, msgcap, "machine is running - stop first");
            return 0;
        }
        if (!ieq(ax, "X") && !ieq(ax, "Y") && !ieq(ax, "Z"))
        {
            snprintf(msg, msgcap, "axis must be X, Y or Z");
            return 0;
        }
        double d = atof(ds);
        if (d <= 0)
            d = 1.0;
        if (d > 500)
            d = 500.0;
        int sign = (dir[0] == '-') ? -1 : 1;
        double cur = axis_get(ax[0]);
        g_m.jog_active = true;
        g_m.jog_axis = ax[0];
        g_m.jog_target = cur + sign * d;
        g_m.jog_remaining = d;
        g_m.jog_feed = 1500.0;
        g_m.mode = MODE_MANUAL;
        g_m.exec = EXEC_READY;
        g_m.sp_tgt = 0.0;
        snprintf(msg, msgcap, "jog %c %c%.2f mm", ax[0],
                 sign > 0 ? '+' : '-', d);
        return 1;
    }
    if (ieq(cmd, "mdi"))
    {
        const char *ax = kv_get(kvs, n, "axis");
        const char *ds = kv_get(kvs, n, "dist");
        if (!ax || !ds)
        {
            snprintf(msg, msgcap, "mdi needs axis and dist");
            return 0;
        }
        if (!ieq(ax, "X") && !ieq(ax, "Y") && !ieq(ax, "Z"))
        {
            snprintf(msg, msgcap, "axis must be X, Y or Z");
            return 0;
        }
        double d = atof(ds);
        if (d == 0)
            d = 20.0;
        if (d > 500)
            d = 500.0;
        if (d < -500)
            d = -500.0;
        double cur = axis_get(ax[0]);
        g_m.mode = MODE_MDI;
        g_m.mdi_active = true;
        g_m.mdi_axis = ax[0];
        g_m.mdi_target = cur + d;
        g_m.mdi_remaining = fabs(d);
        g_m.mdi_feed = 600.0;
        g_m.mdi_sp = 800.0;
        g_m.exec = EXEC_ACTIVE;
        g_m.sp_tgt = g_m.mdi_sp;
        snprintf(msg, msgcap, "mdi move %c %+.2f mm", ax[0], d);
        return 1;
    }
    if (ieq(cmd, "set"))
    {
        const char *k = kv_get(kvs, n, "key");
        const char *v = kv_get(kvs, n, "value");
        if (!k || !v)
        {
            snprintf(msg, msgcap, "set needs key and value");
            return 0;
        }
        if (ieq(k, "Fovr"))
            g_m.feed_ovr = clamp_int(atoi(v), 0, 200);
        else if (ieq(k, "SspeedOvr"))
            g_m.sp_ovr = clamp_int(atoi(v), 0, 200);
        else if (ieq(k, "part_required"))
            g_m.part_required = atoi(v) < 0 ? 0 : atoi(v);
        else if (ieq(k, "part_total"))
            g_m.part_total = atoi(v) < 0 ? 0 : atoi(v);
        else if (ieq(k, "part_current")) {
            g_m.part_current = atoi(v) < 0 ? 0 : atoi(v);
            update_product_index(); /* 件数变化后同步产品下标 */
        }
        else if (ieq(k, "product_index")) {
            g_m.product_index = atoi(v);
            if (g_m.product_index < 0) g_m.product_index = 0;
        }
        else if (ieq(k, "spindle"))
            g_m.sp_tgt = fabs(atof(v));
        else
        {
            snprintf(msg, msgcap,
                     "unknown key (Fovr|SspeedOvr|part_required|part_total|"
                     "part_current|product_index|spindle)");
            return 0;
        }
        snprintf(msg, msgcap, "%s set to %s", k, v);
        return 1;
    }
    if (ieq(cmd, "setpos"))
    {
        const char *ax = kv_get(kvs, n, "axis");
        const char *v = kv_get(kvs, n, "value");
        if (!ax || !v)
        {
            snprintf(msg, msgcap, "setpos needs axis and value");
            return 0;
        }
        if (!ieq(ax, "X") && !ieq(ax, "Y") && !ieq(ax, "Z"))
        {
            snprintf(msg, msgcap, "axis must be X, Y or Z");
            return 0;
        }
        double val = atof(v);
        axis_set(ax[0], val);
        if (ax[0] == 'X')
            g_m.sx = g_m.tx = val;
        else if (ax[0] == 'Y')
            g_m.sy = g_m.ty = val;
        else
            g_m.sz = g_m.tz = val;
        snprintf(msg, msgcap, "position %c set to %.3f", ax[0], val);
        return 1;
    }

    snprintf(msg, msgcap, "unknown command '%s'", cmd);
    return 0;
}

static void build_control_response(char *out, int cap, int ok, const char *msg)
{
    char st[2048];
    state_json(st, sizeof(st));
    snprintf(out, cap, "{\"ok\":%s,\"msg\":\"%s\",\"state\":%s}",
             ok ? "true" : "false", msg, st);
}

static void send_response(sock_t c, const char *ctype, const char *body)
{
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     ctype, (int)strlen(body));
    if (n > 0)
        send(c, hdr, n, 0);
    send(c, body, (int)strlen(body), 0);
}

static const char g_help[] =
    "cnc_sim - controllable CNC machine simulator (SHDR + auto cycle + HTTP control)\r\n"
    "\r\n"
    "SHDR data port : <shdr_port>   (consumed by the MTConnect agent)\r\n"
    "HTTP control   : http://127.0.0.1:<control_port>\r\n"
    "\r\n"
    "Auto cycle is ON by default. Any control command switches to manual\r\n"
    "takeover; send `auto on` to re-enable the auto cycle.\r\n"
    "\r\n"
    "Commands (POST /control with JSON, or GET /control?cmd=...):\r\n"
    "  start                        start the loaded program (AUTOMATIC)\r\n"
    "  stop                         stop / pause at current block\r\n"
    "  hold                         feed hold (INTERRUPTED)\r\n"
    "  resume                       continue after stop/hold\r\n"
    "  reset                        reset to program start\r\n"
    "  estop / estop_release        emergency stop on/off\r\n"
    "  mode <AUTOMATIC|MANUAL|MDI>  switch controller mode\r\n"
    "  program <O1000|O2000|O3000>  load a built-in part program\r\n"
    "                                (each program has multiple products,\r\n"
    "                                switch every 10 parts)\r\n"
    "  alarm <none|spindle|servo|overtravel|overheat|comms|logic|motion|system>\r\n"
    "  jog <axis> <dir> <dist>      manual jog, e.g. jog X + 20\r\n"
    "  mdi <axis> <dist>            single MDI move, e.g. mdi X 20\r\n"
    "  set <key> <value>            Fovr | SspeedOvr | part_required |\r\n"
    "                               part_total | part_current | product_index |\r\n"
    "                               spindle\r\n"
    "  setpos <axis> <value>        force an axis position\r\n"
    "  auto <on|off>                re-enable / disable the auto cycle\r\n"
    "  state                        show current state (same as GET /state)\r\n"
    "\r\n"
    "Examples:\r\n"
    "  curl http://127.0.0.1:<control_port>/state\r\n"
    "  curl -X POST http://127.0.0.1:<control_port>/control -d \"{\\\"cmd\\\":\\\"start\\\"}\"\r\n"
    "  cnc_sim_ctl.exe <control_port> start\r\n"
    "  cnc_sim_ctl.exe <control_port> alarm spindle\r\n";

static void *control_client(void *arg)
{
    sock_t c = (sock_t)(intptr_t)arg;
    char buf[MAX_HTTP];
    int got = 0;

    while (got < (int)sizeof(buf) - 1)
    {
        int r = (int)recv(c, buf + got, (int)sizeof(buf) - 1 - got, 0);
        if (r <= 0)
            break;
        got += r;
        buf[got] = '\0';
        if (strstr(buf, "\r\n\r\n"))
            break;
    }
    buf[got] = '\0';

    char *he = strstr(buf, "\r\n\r\n");
    if (!he)
    {
        sock_close(c);
        return NULL;
    }
    char *body = he + 4;
    int body_have = got - (int)(body - buf);
    int clen = 0;
    {
        char hdr[2048];
        int hn = (int)(body - buf);
        if (hn > (int)sizeof(hdr) - 1)
            hn = (int)sizeof(hdr) - 1;
        memcpy(hdr, buf, (size_t)hn);
        hdr[hn] = '\0';
        char *cl = strstr(hdr, "Content-Length:");
        if (!cl)
            cl = strstr(hdr, "content-length:");
        if (cl)
            clen = atoi(cl + 15);
    }
    while (body_have < clen && got < (int)sizeof(buf) - 1)
    {
        int r = (int)recv(c, body + body_have, clen - body_have, 0);
        if (r <= 0)
            break;
        body_have += r;
        got += r;
    }
    buf[got] = '\0';

    char reqline[768];
    {
        char *nl = strstr(buf, "\r\n");
        if (!nl)
        {
            sock_close(c);
            return NULL;
        }
        size_t l1 = (size_t)(nl - buf);
        if (l1 >= sizeof(reqline))
            l1 = sizeof(reqline) - 1;
        memcpy(reqline, buf, l1);
        reqline[l1] = '\0';
    }

    char method[16] = "", path[512] = "", query[512] = "";
    sscanf(reqline, "%15s %511s", method, path);
    char *q = strchr(path, '?');
    if (q)
    {
        strncpy(query, q + 1, sizeof(query) - 1);
        query[sizeof(query) - 1] = '\0';
        *q = '\0';
    }

    char resp[4096];
    KV kvs[MAX_KV];
    int nkv = 0;
    if (query[0])
        nkv = parse_query(query, kvs, MAX_KV);

    if (strcmp(path, "/state") == 0 || strcmp(path, "/api/state") == 0)
    {
        lock();
        state_json(resp, sizeof(resp));
        unlock();
        send_response(c, "application/json", resp);
    }
    else if (strcmp(path, "/") == 0 || strcmp(path, "/help") == 0)
    {
        send_response(c, "text/plain; charset=utf-8", g_help);
    }
    else if (strcmp(path, "/control") == 0 || strcmp(path, "/api/control") == 0)
    {
        if (clen > 0 && body[0])
            nkv += parse_json(body, kvs + nkv, MAX_KV - nkv);
        char msg[256];
        int ok;
        lock();
        ok = cmd_apply(kvs, nkv, msg, sizeof(msg));
        build_control_response(resp, sizeof(resp), ok, msg);
        unlock();
        send_response(c, "application/json", resp);
    }
    else
    {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"msg\":\"not found\"}");
        send_response(c, "application/json", resp);
    }

    sock_close(c);
    return NULL;
}

typedef struct
{
    void *(*fn)(void *);
    void *arg;
} ThreadCtx;

#ifdef _WIN32
static DWORD WINAPI thread_trampoline(LPVOID p)
{
    ThreadCtx *tc = (ThreadCtx *)p;
    tc->fn(tc->arg);
    free(tc);
    return 0;
}
#endif

static void thread_detach(void *(*fn)(void *), void *arg)
{
#ifdef _WIN32
    ThreadCtx *tc = (ThreadCtx *)malloc(sizeof(ThreadCtx));
    if (!tc)
        return;
    tc->fn = fn;
    tc->arg = arg;
    CreateThread(NULL, 0, thread_trampoline, tc, 0, NULL);
#else
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, fn, arg);
    pthread_attr_destroy(&attr);
#endif
}

static void *control_server(void *arg)
{
    sock_t srv = (sock_t)(intptr_t)arg;
    while (1)
    {
        sock_t c = accept(srv, NULL, NULL);
        if (c != SOCK_BAD)
            thread_detach(control_client, (void *)(intptr_t)c);
    }
    return NULL;
}

/* ===================== 信号处理 ===================== */

#ifndef _WIN32
static volatile bool g_running = true;
static void sig_handler(int sig)
{
    (void)sig;
    g_running = false;
}
#endif

/* ===================== main ===================== */

int main(int argc, char *argv[])
{
    int port = (argc > 1) ? atoi(argv[1]) : 7878;
    int ctl_port = (argc > 2) ? atoi(argv[2]) : port + 2000;
    int interval = (argc > 3) ? atoi(argv[3]) : 500;
    const char *name = (argc > 4) ? argv[4] : "SIM01";
    if (interval < 50)
        interval = 50;

    init_programs();
    lock_init();

    memset(&g_m, 0, sizeof(g_m));
    snprintf(g_m.name, sizeof(g_m.name), "%s", name);
    g_m.avail = true;
    g_m.auto_mode = true;
    g_m.exec = EXEC_READY;
    g_m.mode = MODE_AUTO;
    g_m.feed_ovr = 100;
    g_m.sp_ovr = 100;
    g_m.part_required = 100;
    g_m.z = 10.0;
    g_m.sz = g_m.tz = 10.0;
    g_m.probe_x = 0.0;
    g_m.probe_y = 0.0;
    g_m.probe_z = 10.0;
    g_m.boot_ms = now_ms();
    g_m.state_t0_ms = now_ms();

    clients_init();

#ifndef _WIN32
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
#endif

    printf("[cnc_sim] C99  name=%s  SHDR=%d  control=http://127.0.0.1:%d  interval=%dms  auto_cycle=ON\n",
           g_m.name, port, ctl_port, interval);
    printf("[cnc_sim] control: cnc_sim_ctl.exe %d start|stop|mode|alarm|...\n", ctl_port);

    /* ---- SHDR server (loopback) ---- */
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(srv, (struct sockaddr *)&addr, sizeof(addr));
    listen(srv, 8);
    u_long nonblk = 1;
    ioctlsocket(srv, FIONBIO, &nonblk);
#else
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0)
    {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return 1;
    }
    listen(srv, 8);
    fcntl(srv, F_SETFL, O_NONBLOCK);
#endif

    /* ---- HTTP control server (loopback) ---- */
    sock_t ctl = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in caddr = {0};
    caddr.sin_family = AF_INET;
    caddr.sin_port = htons((unsigned short)ctl_port);
    caddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (ctl == SOCK_BAD ||
        bind(ctl, (struct sockaddr *)&caddr, sizeof(caddr)) != 0)
    {
        printf("[cnc_sim] WARN cannot bind control port %d (HTTP control disabled)\n",
               ctl_port);
        if (ctl != SOCK_BAD)
            sock_close(ctl);
    }
    else
    {
        listen(ctl, 8);
        thread_detach(control_server, (void *)(intptr_t)ctl);
    }

    uint64_t last_tick = now_ms();

#ifndef _WIN32
    while (g_running)
    {
#else
    while (1)
    {
#endif
        /* accept new clients */
#ifdef _WIN32
        SOCKET c = accept(srv, NULL, NULL);
        if (c != INVALID_SOCKET)
            clients_add(c);
#else
        int c = accept(srv, NULL, NULL);
        if (c >= 0)
            clients_add(c);
#endif

        /* simulation tick */
        uint64_t now = now_ms();
        double dt = (now - last_tick) / 1000.0;

        if (dt * 1000.0 >= (double)interval)
        {
            last_tick = now;
            lock();
            auto_cycle_tick();
            sim_step(dt);
            emit_state();
            unlock();
        }

#ifdef _WIN32
        Sleep(20); /* ~20ms sleep */
#endif
#ifndef _WIN32
        usleep(20000); /* ~20ms sleep */
#endif
    }

    return 0;
}
