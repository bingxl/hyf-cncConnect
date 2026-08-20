/*
 * webserver.cpp - MTConnect 数据报表 Web 服务 (C++ rewrite)
 *
 * 轻量 HTTP 服务器（winsock + SQLite），提供：
 *   /api/health /api/machines /api/stats/summary|machining|production|products
 *   /api/live/current   （转发 MTConnect agent /current）
 *   静态资源（web/dist/，SPA 回退）
 *
 * 数据源：stats.db（mtc_stats poll 写入的 samples 表）
 * 用法：webserver.exe [port] [db] [agent_port] [web_root]
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdarg>

#include "db/db.hpp"
#include "db/stats_db.hpp"
#include "config.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {

const size_t MAX_HTTP = 65536;
const int SAMPLE_INTERVAL_DEFAULT = 10;
/* 以下由 config.json 覆盖（web.max_rows / stats.power_gap_max / web.default_bucket_sec） */
static size_t g_max_rows = 1000000;
static long long g_power_gap_max = 90;
static int g_default_bucket_sec = 1800;

/* ---------- small string helpers ---------- */
std::string url_decode(const std::string &src)
{
    std::string dst;
    dst.reserve(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        if (src[i] == '%' && i + 2 < src.size() &&
            isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            auto hexv = [](char c) { return isdigit((unsigned char)c) ? c - '0' : (char)(tolower((unsigned char)c) - 'a' + 10); };
            dst.push_back((char)((hexv(src[i+1]) << 4) | hexv(src[i+2])));
            i += 2;
        } else if (src[i] == '+') {
            dst.push_back(' ');
        } else {
            dst.push_back(src[i]);
        }
    }
    return dst;
}

std::string json_escape(const std::string &in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) out += ' ';
            else out += (char)c;
        }
    }
    return out;
}

/* ---------- query-string ---------- */
class Query {
public:
    std::map<std::string, std::string> params;

    void parse(const std::string &qs)
    {
        params.clear();
        size_t pos = 0;
        while (pos < qs.size() && params.size() < 16) {
            size_t amp = qs.find('&', pos);
            std::string pair = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                params[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
            } else {
                params[url_decode(pair)] = "";
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
    }

    const std::string *get(const std::string &key) const
    {
        auto it = params.find(key);
        return it == params.end() ? nullptr : &it->second;
    }
    long long get_ll(const std::string &key, long long def) const
    {
        const std::string *v = get(key);
        return v ? _atoi64(v->c_str()) : def;
    }
    int get_int(const std::string &key, int def) const
    {
        const std::string *v = get(key);
        return v ? atoi(v->c_str()) : def;
    }
};

/* ---------- sample model + load ---------- */
struct Sample {
    long long ts = 0;
    std::string machine;
    std::string execution;
    std::string mode;
    std::string tmmode;
    std::string comment;
    long long part_total = -1;
    int power = -1; /* 1=开机在线, 0=关机/断联, -1=旧数据无记录(视为开机) */
};

struct SampleSet {
    std::vector<Sample> rows;
    long long first_ts = 0, last_ts = 0;
    int interval_sec = SAMPLE_INTERVAL_DEFAULT;
};

bool is_machining(const Sample &s)
{
    return s.power != 0 && s.execution == "ACTIVE" && s.mode == "AUTOMATIC";
}

/* 相邻样本时间差累加：样本 i 加工中则累加 ts[i+1]-ts[i]（同机器按 ts 有序）。
   相比“样本数 × 平均间隔”，对缺行/间隔不均稳健，误差仅来自采样点之间的状态翻转。 */
static long long mach_sec_range(const SampleSet &ss, const std::string &machine)
{
    long long total = 0;
    const Sample *prev = nullptr;
    for (const auto &s : ss.rows) {
        if (!machine.empty() && s.machine != machine) continue;
        if (prev && is_machining(*prev) && s.ts > prev->ts &&
            s.ts - prev->ts <= g_power_gap_max)
            total += s.ts - prev->ts;
        prev = &s;
    }
    return total;
}

/* 开机时间：相邻样本时间差累加，样本 i 开机（power!=0，NULL 旧数据视为开机）则累加 */
static long long power_sec_range(const SampleSet &ss, const std::string &machine)
{
    long long total = 0;
    const Sample *prev = nullptr;
    for (const auto &s : ss.rows) {
        if (!machine.empty() && s.machine != machine) continue;
        if (prev && prev->power != 0 && s.ts > prev->ts &&
            s.ts - prev->ts <= g_power_gap_max)
            total += s.ts - prev->ts;
        prev = &s;
    }
    return total;
}

bool load_samples(db::Database *db, long long from, long long to,
                  const std::string &machine, SampleSet *ss)
{
    static const char *sqlAll =
        "SELECT ts,machine,execution,mode,tmmode,comment,part_total,power "
        "FROM samples WHERE ts>=?1 AND ts<=?2 ORDER BY ts;";
    static const char *sqlOne =
        "SELECT ts,machine,execution,mode,tmmode,comment,part_total,power "
        "FROM samples WHERE ts>=?1 AND ts<=?2 AND machine=?3 ORDER BY ts;";
    const char *sql = machine.empty() ? sqlAll : sqlOne;

    ss->rows.clear();
    ss->rows.reserve(g_max_rows);
    ss->first_ts = ss->last_ts = 0;
    ss->interval_sec = SAMPLE_INTERVAL_DEFAULT;

    auto st = db->prepare(sql);
    if (!st) return false;
    st->bind_int64(1, from);
    st->bind_int64(2, to);
    if (!machine.empty()) st->bind_text(3, machine);

    while (ss->rows.size() < g_max_rows && st->step()) {
        Sample r;
        r.ts = st->column_int64(0);
        r.machine = st->column_text(1);
        r.execution = st->column_text(2);
        r.mode = st->column_text(3);
        r.tmmode = st->column_text(4);
        r.comment = st->column_text(5);
        r.part_total = st->column_int64(6);
        r.power = st->column_is_null(7) ? -1 : st->column_int(7);
        ss->rows.push_back(std::move(r));
    }

    if (!ss->rows.empty()) {
        ss->first_ts = ss->rows.front().ts;
        ss->last_ts = ss->rows.back().ts;
        if (ss->rows.size() > 1 && ss->last_ts > ss->first_ts) {
            long long d = (ss->last_ts - ss->first_ts) / (long long)(ss->rows.size() - 1);
            if (d >= 1 && d <= 3600) ss->interval_sec = (int)d;
        }
    }
    return true;
}

std::vector<std::string> collect_machines(const SampleSet &ss)
{
    std::vector<std::string> mach;
    for (const auto &s : ss.rows) {
        bool found = false;
        for (const auto &m : mach) if (m == s.machine) { found = true; break; }
        if (!found) mach.push_back(s.machine);
    }
    return mach;
}

/* format "%.*f" etc. via a small append helper */
template <typename... Args>
void fmt(std::string &o, const char *fmtstr, Args... args)
{
    char buf[512];
    _snprintf(buf, sizeof(buf), fmtstr, args...);
    o += buf;
}

/* ---------- aggregation ---------- */
std::string api_summary(const SampleSet &ss)
{
    auto mach = collect_machines(ss);
    std::string o = "{\"items\":[";
    bool first = true;
    for (const auto &name : mach) {
        long long cnt = 0, sample_cnt = 0, mfirst = 0, mlast = 0;
        long long first_total = -1, last_total = -1;
        const Sample *last = nullptr;

        for (const auto &s : ss.rows) {
            if (s.machine != name) continue;
            if (!mfirst) mfirst = s.ts;
            mlast = s.ts;
            sample_cnt++;
            if (is_machining(s)) cnt++;
            if (s.part_total >= 0) {
                if (first_total < 0) first_total = s.part_total;
                last_total = s.part_total;
            }
            last = &s;
        }
        long long mach_sec = mach_sec_range(ss, name);
        long long power_sec = power_sec_range(ss, name);
        /* 产量 = part_total 正增量求和：计数可能清零/重启（如批次结束、断电），
           首尾差值会漏算；只要相邻样本 part_total 上升就累加差值 */
        long long produced = 0;
        {
            const Sample *prev = nullptr;
            for (const auto &s : ss.rows) {
                if (s.machine != name) continue;
                if (prev && prev->part_total >= 0 && s.part_total >= 0 &&
                    s.part_total > prev->part_total)
                    produced += s.part_total - prev->part_total;
                prev = &s;
            }
        }
        double util = power_sec > 0 ? (double)mach_sec / (double)power_sec : 0.0;
        if (util > 1.0) util = 1.0;

        std::string last_exec, last_mode, last_comment;
        long long last_ts = 0;
        if (last) {
            last_exec = json_escape(last->execution);
            last_mode = json_escape(last->mode);
            last_comment = json_escape(last->comment);
            last_ts = last->ts;
        }
        if (!first) o += ",";
        first = false;
        fmt(o,
            "{\"machine\":\"%s\",\"mach_sec\":%lld,\"power_sec\":%lld,\"util_rate\":%.3f,"
            "\"part_total_start\":%lld,\"part_total_end\":%lld,\"produced\":%lld,"
            "\"sample_count\":%lld,\"machining_count\":%lld,"
            "\"last\":{\"execution\":\"%s\",\"mode\":\"%s\",\"comment\":\"%s\",\"ts\":%lld}}",
            name.c_str(), mach_sec, power_sec, util, first_total, last_total, produced,
            sample_cnt, cnt, last_exec.c_str(), last_mode.c_str(), last_comment.c_str(), last_ts);
    }
    o += "]}";
    return o;
}

std::string api_machining(const SampleSet &ss, int bucket, const std::string &machine)
{
    std::string o;
    fmt(o, "{\"machine\":\"%s\",\"bucket\":%d,\"interval_sec\":%d,\"points\":[",
        machine.c_str(), bucket, ss.interval_sec);

    long long bstart = (ss.first_ts / bucket) * bucket;
    long long bend = (ss.last_ts / bucket) * bucket;
    std::map<long long, long long> secs, psecs, cnts, scnts;
    /* 相邻样本差必须按“同一台机床”计算：ALL 模式下各行按 ts 全局交错，
       用单个 prev 会把不同机床的样本当成相邻，导致加工/开机时间趋近 0 */
    std::map<std::string, const Sample *> prevs;
    for (const auto &s : ss.rows) {
        long long b = (s.ts / bucket) * bucket;
        scnts[b]++;
        if (is_machining(s)) cnts[b]++;
        const Sample *prev = prevs[s.machine];
        if (prev && s.ts > prev->ts && s.ts - prev->ts <= g_power_gap_max) {
            long long t0 = prev->ts, t1 = s.ts;
            for (long long bb = (t0 / bucket) * bucket; bb < t1; bb += bucket) {
                long long lo = bb > t0 ? bb : t0;
                long long hi = (bb + bucket < t1) ? bb + bucket : t1;
                if (hi <= lo) continue;
                if (is_machining(*prev)) secs[bb] += hi - lo;
                if (prev->power != 0) psecs[bb] += hi - lo;
            }
        }
        prevs[s.machine] = &s;
    }
    bool first = true;
    for (long long b = bstart; b <= bend; b += bucket) {
        if (scnts[b] > 0) {
            if (!first) o += ",";
            first = false;
            fmt(o, "{\"bucket_ts\":%lld,\"mach_sec\":%lld,\"power_sec\":%lld,"
                   "\"machining_count\":%lld,\"sample_count\":%lld}",
                b, secs[b], psecs[b], cnts[b], scnts[b]);
        }
    }
    o += "]}";
    return o;
}

std::string api_production(const SampleSet &ss, int bucket, const std::string &machine)
{
    std::string o;
    fmt(o, "{\"machine\":\"%s\",\"bucket\":%d,\"points\":[", machine.c_str(), bucket);

    long long bstart = (ss.first_ts / bucket) * bucket;
    long long bend = (ss.last_ts / bucket) * bucket;
    std::map<long long, long long> produced, firstmap, lastmap;
    std::map<std::string, const Sample *> prevs;
    /* 产量 = 每台机床 part_total 正增量求和，增量归到“后一个样本”所在桶；
       首/末值仅作展示（计数可能清零，不代表产量） */
    for (const auto &s : ss.rows) {
        long long b = (s.ts / bucket) * bucket;
        const Sample *prev = prevs[s.machine];
        if (prev && prev->part_total >= 0 && s.part_total >= 0 &&
            s.part_total > prev->part_total)
            produced[b] += s.part_total - prev->part_total;
        prevs[s.machine] = &s;
        if (s.part_total >= 0) {
            if (!firstmap.count(b)) firstmap[b] = s.part_total;
            lastmap[b] = s.part_total;
        }
    }
    bool first = true;
    for (long long b = bstart; b <= bend; b += bucket) {
        if (!firstmap.count(b)) continue;
        if (!first) o += ",";
        first = false;
        fmt(o, "{\"bucket_ts\":%lld,\"produced\":%lld,\"start_total\":%lld,\"end_total\":%lld}",
            b, produced[b], firstmap[b], lastmap[b]);
    }
    o += "]}";
    return o;
}

std::string api_products(const SampleSet &ss, const std::string &label,
                         const std::string &machine)
{
    struct Prod {
        std::string comment;
        long long start_total = -1, end_total = -1, produced = 0, mach_sec = 0,
                  first_ts = 0, last_ts = 0;
        bool have = false;
    };
    std::vector<Prod> list;

    for (const auto &s : ss.rows) {
        if (!machine.empty() && s.machine != machine) continue;
        Prod *cur = nullptr;
        for (auto &p : list) if (p.comment == s.comment) { cur = &p; break; }
        if (!cur && list.size() < 256) {
            list.push_back(Prod());
            cur = &list.back();
            cur->comment = s.comment;
        }
        if (!cur) continue;
        if (s.part_total >= 0) {
            if (cur->start_total < 0) cur->start_total = s.part_total;
            cur->end_total = s.part_total;
        }
        if (!cur->first_ts) cur->first_ts = s.ts;
        cur->last_ts = s.ts;
        cur->have = true;
    }
    /* 加工时间：相邻样本时间差累加，按前一个样本的产品归组 */
    std::map<std::string, const Sample *> prevs;
    for (const auto &s : ss.rows) {
        if (!machine.empty() && s.machine != machine) continue;
        const Sample *prev = prevs[s.machine];
        if (prev) {
            if (is_machining(*prev) && s.ts > prev->ts &&
                s.ts - prev->ts <= g_power_gap_max) {
                for (auto &p : list)
                    if (p.comment == prev->comment) {
                        p.mach_sec += s.ts - prev->ts;
                        break;
                    }
            }
            /* 产量：part_total 正增量求和（计数可能清零，首尾差值会漏算） */
            if (prev->part_total >= 0 && s.part_total >= 0 &&
                s.part_total > prev->part_total) {
                for (auto &p : list)
                    if (p.comment == prev->comment) {
                        p.produced += s.part_total - prev->part_total;
                        break;
                    }
                }
        }
        prevs[s.machine] = &s;
    }

    std::string o;
    fmt(o, "{\"machine\":\"%s\",\"items\":[", label.c_str());
    bool first = true;
    for (const auto &p : list) {
        if (!p.have) continue;
        if (!first) o += ",";
        first = false;
        fmt(o,
            "{\"comment\":\"%s\",\"produced\":%lld,\"mach_sec\":%lld,"
            "\"start_total\":%lld,\"end_total\":%lld,\"first_ts\":%lld,\"last_ts\":%lld}",
            json_escape(p.comment).c_str(), p.produced, p.mach_sec,
            p.start_total, p.end_total, p.first_ts, p.last_ts);
    }
    o += "]}";
    return o;
}

/* ---------------- 班次统计（白班/夜班） ---------------- */

/* 班次定义（AGENTS.md）：
   周一~周六：白班 08:30-20:30，夜班 20:30-次日 08:30
   周日    ：白班 08:30-17:00，无夜班 */
struct ShiftWin {
    bool day = true;
    long long start = 0, end = 0; /* 完整班次 [start,end) */
    long long ws = 0, we = 0;     /* 与查询区间相交的窗口 [ws,we) */
    long long dateTs = 0;
    char date[16];
    char label[48];
};

static long long local_mktime(int y, int mo, int d, int h, int mi)
{
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_isdst = -1;
    return (long long)mktime(&t);
}

static void build_shifts(long long from, long long to, std::vector<ShiftWin> &out)
{
    time_t f = (time_t)from;
    struct tm tf;
    localtime_s(&tf, &f);
    time_t te = (time_t)to;
    struct tm tt;
    localtime_s(&tt, &te);

    struct tm cur = tf;
    for (int guard = 0; guard < 512; guard++) {
        if (cur.tm_year > tt.tm_year ||
            (cur.tm_year == tt.tm_year && cur.tm_mon > tt.tm_mon) ||
            (cur.tm_year == tt.tm_year && cur.tm_mon == tt.tm_mon &&
             cur.tm_mday > tt.tm_mday))
            break;

        int y = cur.tm_year + 1900, mo = cur.tm_mon + 1, d = cur.tm_mday;
        long long day0 = local_mktime(y, mo, d, 0, 0);
        int wday = cur.tm_wday; /* 0=Sunday */

        ShiftWin day;
        day.day = true;
        day.start = day0 + 8 * 3600 + 30 * 60;
        day.end = (wday == 0) ? day0 + 17 * 3600 : day0 + 20 * 3600 + 30 * 60;
        snprintf(day.date, sizeof(day.date), "%04d-%02d-%02d", y, mo, d);
        snprintf(day.label, sizeof(day.label),
                 wday == 0 ? "白班(周日 08:30-17:00)" : "白班 08:30-20:30");
        day.dateTs = day0;
        day.ws = day.start > from ? day.start : from;
        day.we = day.end < to ? day.end : to;
        if (day.ws < day.we) out.push_back(day);

        if (wday != 0) {
            ShiftWin night;
            night.day = false;
            night.start = day0 + 20 * 3600 + 30 * 60;
            night.end = day0 + 32 * 3600 + 30 * 60; /* 次日 08:30 */
            snprintf(night.date, sizeof(night.date), "%04d-%02d-%02d", y, mo, d);
            snprintf(night.label, sizeof(night.label), "夜班 20:30-08:30");
            night.dateTs = day0;
            night.ws = night.start > from ? night.start : from;
            night.we = night.end < to ? night.end : to;
            if (night.ws < night.we) out.push_back(night);
        }

        cur.tm_mday++;
        time_t nx = mktime(&cur);
        localtime_s(&cur, &nx);
    }
}

struct ShiftMach {
    std::string name;
    long long mach_sec = 0, power_sec = 0, produced = 0;
    std::vector<std::pair<std::string, long long>> products;
};

/* 在 [ws,we) 内按机床聚合：加工/开机时间（相邻样本差）、产量与分产品件数 */
static void shift_aggregate(const SampleSet &ss, const std::string &filter,
                            long long ws, long long we,
                            std::vector<ShiftMach> &machines, ShiftMach *fleet)
{
    std::map<std::string, long long> machSec, powerSec, produced;
    std::map<std::string, std::map<std::string, long long>> prodByComment;
    std::map<std::string, const Sample *> prevs;
    std::set<std::string> present; /* 窗口内出现过的机床（含纯待机） */

    for (const auto &s : ss.rows) {
        if (s.ts < ws || s.ts >= we) continue;
        if (!filter.empty() && s.machine != filter) continue;
        present.insert(s.machine);
        const Sample *prev = prevs[s.machine];
        if (prev) {
            long long dt = s.ts - prev->ts;
            if (dt > 0 && dt <= g_power_gap_max) {
                if (is_machining(*prev)) machSec[s.machine] += dt;
                if (prev->power != 0) powerSec[s.machine] += dt;
            }
            if (prev->part_total >= 0 && s.part_total >= 0 &&
                s.part_total > prev->part_total) {
                long long inc = s.part_total - prev->part_total;
                produced[s.machine] += inc;
                prodByComment[s.machine][prev->comment] += inc;
            }
        }
        prevs[s.machine] = &s;
    }

    for (const auto &name : present) {
        ShiftMach m;
        m.name = name;
        m.mach_sec = machSec[name];
        m.power_sec = powerSec[name];
        m.produced = produced[name];
        for (const auto &pc : prodByComment[name])
            if (!pc.first.empty() && pc.second > 0)
                m.products.push_back(pc);
        std::sort(m.products.begin(), m.products.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });
        machines.push_back(std::move(m));
    }
    std::sort(machines.begin(), machines.end(),
              [](const ShiftMach &a, const ShiftMach &b) { return a.name < b.name; });

    if (fleet) {
        std::map<std::string, long long> fp;
        for (const auto &m : machines) {
            fleet->mach_sec += m.mach_sec;
            fleet->power_sec += m.power_sec;
            fleet->produced += m.produced;
            for (const auto &p : m.products) fp[p.first] += p.second;
        }
        for (const auto &pc : fp) fleet->products.push_back(pc);
        std::sort(fleet->products.begin(), fleet->products.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });
    }
}

static std::string api_shifts(const SampleSet &ss, long long from, long long to,
                              const std::string &machine)
{
    std::vector<ShiftWin> wins;
    build_shifts(from, to, wins);

    std::string o;
    fmt(o, "{\"from\":%lld,\"to\":%lld,\"shifts\":[", from, to);
    bool firstShift = true;
    for (const auto &w : wins) {
        std::vector<ShiftMach> machs;
        ShiftMach fleet;
        shift_aggregate(ss, machine, w.ws, w.we, machs, &fleet);

        if (!firstShift) o += ",";
        firstShift = false;
        fmt(o, "{\"shift\":\"%s\",\"date\":\"%s\",\"date_ts\":%lld,"
               "\"start\":%lld,\"end\":%lld,\"ws\":%lld,\"we\":%lld,"
               "\"label\":\"%s\",\"machines\":[",
            w.day ? "day" : "night", w.date, w.dateTs,
            w.start, w.end, w.ws, w.we, w.label);
        bool firstMach = true;
        for (const auto &m : machs) {
            if (!firstMach) o += ",";
            firstMach = false;
            double util = m.power_sec > 0 ? (double)m.mach_sec / (double)m.power_sec : 0.0;
            if (util > 1.0) util = 1.0;
            fmt(o, "{\"machine\":\"%s\",\"mach_sec\":%lld,\"power_sec\":%lld,"
                   "\"util_rate\":%.3f,\"produced\":%lld,\"products\":[",
                m.name.c_str(), m.mach_sec, m.power_sec, util, m.produced);
            bool firstP = true;
            for (const auto &p : m.products) {
                if (!firstP) o += ",";
                firstP = false;
                fmt(o, "{\"comment\":\"%s\",\"produced\":%lld}",
                    json_escape(p.first).c_str(), p.second);
            }
            o += "]}";
        }
        o += "],";
        double fut = fleet.power_sec > 0 ? (double)fleet.mach_sec / (double)fleet.power_sec : 0.0;
        if (fut > 1.0) fut = 1.0;
        fmt(o, "\"fleet\":{\"mach_sec\":%lld,\"power_sec\":%lld,"
               "\"util_rate\":%.3f,\"produced\":%lld,\"products\":[",
            fleet.mach_sec, fleet.power_sec, fut, fleet.produced);
        bool firstFp = true;
        for (const auto &p : fleet.products) {
            if (!firstFp) o += ",";
            firstFp = false;
            fmt(o, "{\"comment\":\"%s\",\"produced\":%lld}",
                json_escape(p.first).c_str(), p.second);
        }
        o += "]}}";
    }
    o += "]}";
    return o;
}

std::string http_get(int port, const std::string &path, bool *ok)
{
    std::string body;
    *ok = false;
    HINTERNET hSession = WinHttpOpen(L"mtc-web/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return body;
    HINTERNET hConn = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)port, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return body; }
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", L"/current", nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (hReq) {
        if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD avail = 0, read = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                char buf[8192];
                if (!WinHttpReadData(hReq, buf, min(avail, (DWORD)sizeof(buf)), &read)) break;
                body.append(buf, read);
                if (read < avail) break;
            }
            *ok = true;
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);
    return body;
}

std::string api_live_current(int agent_port)
{
    bool ok = false;
    std::string xml = http_get(agent_port, "/current", &ok);

    std::string o = "{\"items\":[";
    if (ok) {
        const char *p = xml.c_str();
        bool first = true;
        while ((p = strstr(p, "<DeviceStream ")) != nullptr) {
            const char *block_end = strstr(p, "</DeviceStream>");
            if (!block_end) break;
            const char *scan = p;
            std::string avail = "false", exec, mode, tmmode, prog, cmt, pt;

            const char *n = strstr(scan, "name=\"");
            std::string name;
            if (n) {
                n += 6;
                const char *e = strchr(n, '"');
                if (e) name.assign(n, e - n);
            }
            if (!name.empty() && name.compare(0, 5, "Agent") != 0) {
                if (strstr(scan, "<Availability") && strstr(scan, "AVAILABLE"))
                    avail = "true";
                auto between = [&](const char *open, size_t max) {
                    std::string out;
                    const char *s = strstr(scan, open);
                    if (s) {
                        const char *g = strchr(s, '>');
                        const char *l = g ? strchr(g, '<') : nullptr;
                        if (g && l && (size_t)(l - g - 1) < max)
                            out.assign(g + 1, l - g - 1);
                    }
                    return out;
                };
                exec = between("<Execution", 32);
                mode = between("<ControllerMode", 32);
                prog = between("<Program ", 64);
                cmt = between("<ProgramComment", 256);
                if ((n = strstr(scan, "name=\"tmMode\"")) != nullptr) {
                    const char *g = strchr(n, '>'), *l = g ? strchr(g, '<') : nullptr;
                    if (g && l && (size_t)(l - g - 1) < 32) tmmode.assign(g + 1, l - g - 1);
                }
                if ((n = strstr(scan, "name=\"partTotal\"")) != nullptr) {
                    const char *g = strchr(n, '>'), *l = g ? strchr(g, '<') : nullptr;
                    if (g && l && (size_t)(l - g - 1) < 32) pt.assign(g + 1, l - g - 1);
                }
                if (!first) o += ",";
                first = false;
                std::string ptv = (!pt.empty() && pt.find_first_not_of("0123456789-") == std::string::npos) ? pt : "null";
                fmt(o,
                    "{\"name\":\"%s\",\"available\":%s,\"execution\":\"%s\",\"mode\":\"%s\","
                    "\"tmmode\":\"%s\",\"program\":\"%s\",\"comment\":\"%s\",\"part_total\":%s}",
                    json_escape(name).c_str(), avail.c_str(), json_escape(exec).c_str(),
                    json_escape(mode).c_str(), json_escape(tmmode).c_str(),
                    json_escape(prog).c_str(), json_escape(cmt).c_str(), ptv.c_str());
            }
            p = block_end + strlen("</DeviceStream>");
        }
    }
    o += "]}";
    return o;
}

/* ---------- response helpers (looped send) ---------- */
class Response {
public:
    SOCKET c = INVALID_SOCKET;

    bool send_all(const char *buf, int len)
    {
        int sent = 0;
        while (sent < len) {
            int n = send(c, buf + sent, len - sent, 0);
            if (n == SOCKET_ERROR) return false;
            if (n == 0) break;
            sent += n;
        }
        return true;
    }
    void send_head(int code, const char *ctype, long long len)
    {
        const char *reason = (code == 200) ? "OK"
            : (code == 204) ? "No Content"
            : (code == 400) ? "Bad Request"
            : (code == 404) ? "Not Found"
            : (code == 405) ? "Method Not Allowed"
            : (code == 500) ? "Internal Server Error" : "OK";
        char hdr[512];
        _snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n"
            "\r\n", code, reason, ctype, len);
        send_all(hdr, (int)strlen(hdr));
    }
    void send_json(int code, const std::string &body)
    {
        send_head(code, "application/json; charset=utf-8", (long long)body.size());
        send_all(body.c_str(), (int)body.size());
    }
    void send_error(int code, const std::string &msg)
    {
        send_json(code, "{\"error\":\"" + json_escape(msg) + "\"}");
    }
};

void serve_file(Response &r, const std::string &web_root, const std::string &path)
{
    std::string p = path;
    if (p.empty()) p = "index.html";
    while (!p.empty() && p[0] == '/') p.erase(p.begin());
    if (p.find("..") != std::string::npos || p.find(':') != std::string::npos) {
        r.send_error(400, "bad path");
        return;
    }
    std::string full = web_root + "/" + p;

    FILE *f = fopen(full.c_str(), "rb");
    if (!f) {
        full = web_root + "/index.html";
        f = fopen(full.c_str(), "rb");
        if (!f) { r.send_error(404, "not found"); return; }
        p = "index.html";
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<char> buf((size_t)len);
    if (fread(buf.data(), 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        r.send_error(500, "read");
        return;
    }
    fclose(f);

    const char *mime = "application/octet-stream";
    size_t dot = p.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = p.substr(dot);
        if (ext == ".html") mime = "text/html; charset=utf-8";
        else if (ext == ".js") mime = "application/javascript";
        else if (ext == ".css") mime = "text/css";
        else if (ext == ".json") mime = "application/json";
        else if (ext == ".svg") mime = "image/svg+xml";
        else if (ext == ".png") mime = "image/png";
        else if (ext == ".ico") mime = "image/x-icon";
    }

    r.send_head(200, mime, len);
    r.send_all(buf.data(), (int)len);
}

/* ---------- request handling ---------- */
struct ServerConfig {
    db::Config dbcfg;
    int agent_port = 5000;
    std::string web_root = "web/dist";
};

void handle_request(SOCKET c, const ServerConfig &cfg,
                    const std::string &method, const std::string &path,
                    const std::string &query)
{
    Response r;
    r.c = c;
    Query q;
    q.parse(query);

    if (method == "OPTIONS") {
        r.send_head(204, "text/plain", 0);
        return;
    }
    if (method != "GET") {
        r.send_error(405, "method not allowed");
        return;
    }

    /* 每个请求独立打开连接：SQLite 以单线程模式编译，多线程共享句柄不安全；
       迁移到 MySQL 后这里可换成连接池。 */
    std::string derr;
    auto dbs = db::open(cfg.dbcfg, &derr);
    if (!dbs) {
        r.send_error(500, "db open failed: " + derr);
        return;
    }

    if (path == "/api/health") {
        long long rows = 0, first = 0, last = 0;
        if (auto st = dbs->prepare(
                "SELECT COUNT(*), MIN(ts), MAX(ts) FROM samples;")) {
            if (st->step()) {
                rows = st->column_int64(0);
                first = st->column_int64(1);
                last = st->column_int64(2);
            }
        }
        std::string o;
        fmt(o, "{\"status\":\"ok\",\"server_time\":%lld,\"db_rows\":%lld,"
               "\"first_sample\":%lld,\"last_sample\":%lld}",
            (long long)time(nullptr), rows, first, last);
        r.send_json(200, o);
        return;
    }

    if (path == "/api/machines") {
        std::string o = "{\"machines\":[";
        bool first = true;
        if (auto st = dbs->prepare(
                "SELECT machine, MIN(ts), MAX(ts) FROM samples "
                "GROUP BY machine ORDER BY machine;")) {
            while (st->step()) {
                std::string m = st->column_text(0);
                if (!first) o += ",";
                first = false;
                fmt(o, "{\"name\":\"%s\",\"first_ts\":%lld,\"last_ts\":%lld}",
                    m.c_str(), st->column_int64(1), st->column_int64(2));
            }
        }
        o += "]}";
        r.send_json(200, o);
        return;
    }

    if (path == "/api/alarms/current" || path == "/api/alarms/history") {
        bool current = (path == "/api/alarms/current");
        long long from = q.get_ll("from", (long long)time(nullptr) - 86400);
        long long to = q.get_ll("to", (long long)time(nullptr));
        std::string machine;
        if (const std::string *v = q.get("machine")) machine = *v;

        const char *sql;
        if (current) {
            sql = "SELECT machine,item_id,item_type,state,first_ts,last_ts,end_ts,active "
                  "FROM alarms WHERE active=1 ORDER BY last_ts DESC;";
        } else if (!machine.empty()) {
            sql = "SELECT machine,item_id,item_type,state,first_ts,last_ts,end_ts,active "
                  "FROM alarms WHERE last_ts>=?1 AND first_ts<=?2 AND machine=?3 "
                  "ORDER BY last_ts DESC;";
        } else {
            sql = "SELECT machine,item_id,item_type,state,first_ts,last_ts,end_ts,active "
                  "FROM alarms WHERE last_ts>=?1 AND first_ts<=?2 "
                  "ORDER BY last_ts DESC;";
        }
        auto st = dbs->prepare(sql);
        if (!st) {
            r.send_error(500, "db query failed: " + dbs->last_error());
            return;
        }
        if (!current) {
            st->bind_int64(1, from);
            st->bind_int64(2, to);
            if (!machine.empty()) st->bind_text(3, machine);
        }

        std::string o = "{\"items\":[";
        bool first = true;
        while (st->step()) {
            if (!first) o += ",";
            first = false;
            std::string endTs = st->column_is_null(6) ? "null" : std::to_string(st->column_int64(6));
            fmt(o,
                "{\"machine\":\"%s\",\"item_id\":\"%s\",\"item_type\":\"%s\","
                "\"state\":\"%s\",\"first_ts\":%lld,\"last_ts\":%lld,"
                "\"end_ts\":%s,\"active\":%d}",
                json_escape(st->column_text(0)).c_str(),
                json_escape(st->column_text(1)).c_str(),
                json_escape(st->column_text(2)).c_str(),
                json_escape(st->column_text(3)).c_str(),
                st->column_int64(4), st->column_int64(5),
                endTs.c_str(), st->column_int(7));
        }
        o += "]}";
        r.send_json(200, o);
        return;
    }

    if (path.compare(0, 11, "/api/stats/") == 0) {
        long long from = q.get_ll("from", (long long)time(nullptr) - 86400);
        long long to = q.get_ll("to", (long long)time(nullptr));
        int bucket = q.get_int("bucket", g_default_bucket_sec);
        std::string machine;
        if (const std::string *v = q.get("machine")) machine = *v;
        bool all = (machine == "ALL");          /* machine=ALL -> 全厂汇总 */
        if (all) machine.clear();
        if (bucket < 60) bucket = 60;

        SampleSet ss;
        if (!load_samples(dbs.get(), from, to, machine, &ss)) {
            r.send_error(500, "db query failed");
            return;
        }
        if (ss.rows.empty()) {
            if (path == "/api/stats/shifts") {
                std::string body = "{\"from\":" + std::to_string(from) +
                                   ",\"to\":" + std::to_string(to) + ",\"shifts\":[]}";
                r.send_json(200, body);
            } else {
                r.send_json(200, "{\"items\":[]}");
            }
            return;
        }

        std::string out;
        if (path == "/api/stats/shifts") {
            out = api_shifts(ss, from, to, machine);
        } else if (path == "/api/stats/summary") {
            out = api_summary(ss);
        } else if (path == "/api/stats/machining") {
            out = api_machining(ss, bucket,
                                all ? "ALL" : (machine.empty() ? ss.rows.front().machine : machine));
        } else if (path == "/api/stats/production") {
            out = api_production(ss, bucket,
                                 all ? "ALL" : (machine.empty() ? ss.rows.front().machine : machine));
        } else if (path == "/api/stats/products") {
            out = api_products(ss,
                               all ? "ALL" : (machine.empty() ? ss.rows.front().machine : machine),
                               machine);
        } else {
            r.send_error(404, "unknown endpoint");
            return;
        }
        r.send_json(200, out);
        return;
    }

    if (path == "/api/live/current") {
        r.send_json(200, api_live_current(cfg.agent_port));
        return;
    }

    if (path.compare(0, 5, "/api/") == 0) {
        r.send_error(404, "unknown api endpoint");
        return;
    }

    serve_file(r, cfg.web_root, path);
}

struct ThreadArg {
    SOCKET c = INVALID_SOCKET;
    ServerConfig cfg;
};

DWORD WINAPI client_thread(LPVOID lp)
{
    ThreadArg *arg = (ThreadArg *)lp;
    SOCKET c = arg->c;
    char req[MAX_HTTP];
    int n = recv(c, req, sizeof(req) - 1, 0);
    if (n > 0) {
        req[n] = '\0';
        std::string line(req);
        size_t nl = line.find("\r\n");
        if (nl != std::string::npos) line = line.substr(0, nl);

        std::string method, target;
        std::string path, query;
        size_t sp1 = line.find(' ');
        size_t sp2 = sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);
        if (sp1 != std::string::npos && sp2 != std::string::npos) {
            method = line.substr(0, sp1);
            target = line.substr(sp1 + 1, sp2 - sp1 - 1);
            size_t qm = target.find('?');
            if (qm != std::string::npos) {
                query = target.substr(qm + 1);
                target = target.substr(0, qm);
            }
            path = url_decode(target);
        }
        handle_request(c, arg->cfg, method, path, query);
    }
    closesocket(c);
    delete arg;
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    cfg::Config c;
    std::string cerr;
    cfg::load(c, "", &cerr);
    if (!cerr.empty()) fprintf(stderr, "[webserver] %s\n", cerr.c_str());

    int port = c.web_port;
    ServerConfig cfg;
    cfg.dbcfg.backend = c.db_type == "mysql" ? db::Backend::Mysql
                       : c.db_type == "postgres" ? db::Backend::Postgres
                                                 : db::Backend::Sqlite;
    cfg.dbcfg.file = c.db_path;
    cfg.dbcfg.host = c.db_host;
    cfg.dbcfg.port = c.db_port;
    cfg.dbcfg.user = c.db_user;
    cfg.dbcfg.password = c.db_password;
    cfg.dbcfg.database = c.db_database;
    cfg.agent_port = c.web_agent_port;
    cfg.web_root = c.web_root;
    g_max_rows = c.web_max_rows;
    g_power_gap_max = c.power_gap_max;
    g_default_bucket_sec = c.web_default_bucket_sec;

    /* 命令行参数覆盖（兼容旧用法） */
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) cfg.dbcfg.file = argv[2];
    if (argc > 3) cfg.agent_port = atoi(argv[3]);
    if (argc > 4) cfg.web_root = argv[4];

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind %d failed\n", port);
        return 1;
    }
    listen(srv, 16);
    printf("webserver listening on %d (db=%s agent=%d web=%s)\n",
           port, cfg.dbcfg.file.c_str(), cfg.agent_port, cfg.web_root.c_str());

    u_long mode = 1;
    ioctlsocket(srv, FIONBIO, &mode);

    while (true) {
        SOCKET c = accept(srv, nullptr, nullptr);
        if (c != INVALID_SOCKET) {
            ThreadArg *arg = new ThreadArg();
            arg->c = c;
            arg->cfg = cfg;
            HANDLE h = CreateThread(nullptr, 0, client_thread, arg, 0, nullptr);
            if (h) CloseHandle(h);
            else { delete arg; closesocket(c); }
        }
        Sleep(10);
    }

    closesocket(srv);
    WSACleanup();
    return 0;
}
