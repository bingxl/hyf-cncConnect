/*
 * mtc_stats.cpp - MTConnect 数据采集统计工具（C++ 版）
 *
 *  stream : 流式增量采集。每次请求 agent 的 /sample?from=<lastSeq+1>&count=...
 *           只拉取自上次序列号以来的新数据，按事件 sequence 增量续传；
 *           响应按块（chunk）读取并即时解析，兼容长连接流式 agent，
 *           也兼容断开式响应（本项目的 agent 即每次返回完整文档后关闭）。
 *  poll   : 旧版兼容，周期性拉取 /current 全量快照。
 *  report : 按时间/机器/产品统计加工时间与产量。
 *
 * Usage:
 *   mtc_stats.exe stream [http_port] [db] [interval_ms]
 *   mtc_stats.exe poll   [http_port] [interval_sec] [db]
 *   mtc_stats.exe report [db] [bucket_sec] [from_unix] [to_unix]
 *
 * 统计口径（与 webserver / cnc_sampler 一致）：
 *   加工中 = execution==ACTIVE && mode==AUTOMATIC && tmmode==0
 *   产量   = part_total (#6712) 差值
 *   产品   = program comment
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sqlite3.h"

#pragma comment(lib, "winhttp.lib")

namespace {

constexpr int kDefaultPort = 5000;
constexpr int kDefaultIntervalMs = 5000;   /* stream 默认采集间隔 */
constexpr int kSampleCount = 1000;         /* 每次 /sample 最多拉取的事件数 */
constexpr int kReceiveTimeoutMs = 15000;   /* 流式读取无数据时的超时 */

/* ------------------------------------------------------------------ */
/* 小工具                                                              */
/* ------------------------------------------------------------------ */

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

long long parse_ll(const char *s)
{
    if (!s || !*s) return -1;
    char *end = nullptr;
    long long v = _strtoi64(s, &end, 10);
    return (end == s) ? -1 : v;
}

/* 提取 s 中第一个 name="..." 的属性值（用于 DeviceStream 块） */
std::string attr_of(const std::string &s, const char *name)
{
    std::string pat = std::string(name) + "=\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    size_t e = s.find('"', p);
    if (e == std::string::npos) return "";
    return s.substr(p, e - p);
}

/* 从 <tag ...>value</tag> 提取 value（含 sequence 属性）；返回闭合标签后的位置 */
size_t next_tag_value(const std::string &s, size_t pos, const char *tag,
                      std::string &out, long long *seq)
{
    std::string open = std::string("<") + tag;
    size_t p = s.find(open, pos);
    while (p != std::string::npos) {
        char c = (p + open.size() < s.size()) ? s[p + open.size()] : '\0';
        if (c == ' ' || c == '>') {
            size_t gt = s.find('>', p);
            if (gt == std::string::npos) return std::string::npos;
            std::string close = "</" + std::string(tag) + ">";
            size_t ce = s.find(close, gt);
            if (ce == std::string::npos) return std::string::npos; /* 块未完整，等待更多数据 */
            out = s.substr(gt + 1, ce - gt - 1);
            if (seq) *seq = parse_ll(attr_of(s.substr(p, gt - p), "sequence").c_str());
            return ce + close.size();
        }
        p = s.find(open, p + 1);
    }
    return std::string::npos;
}

/* 提取 <Sample ... name="partTotal" ...>value</Sample> 之类的命名数据项 */
bool named_sample_value(const std::string &s, const char *itemName,
                        std::string &out, long long *seq)
{
    std::string pat = std::string("name=\"") + itemName + "\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return false;
    size_t open = s.rfind('<', p);
    size_t gt = s.find('>', p);
    if (open == std::string::npos || gt == std::string::npos || gt < open) return false;
    size_t lt = s.find('<', gt + 1);
    if (lt == std::string::npos) return false;
    out = s.substr(gt + 1, lt - gt - 1);
    if (seq) *seq = parse_ll(attr_of(s.substr(open, gt - open), "sequence").c_str());
    return true;
}

/* ------------------------------------------------------------------ */
/* 采样状态                                                            */
/* ------------------------------------------------------------------ */

struct MachineState {
    std::string name;
    std::string execution;
    std::string mode;
    std::string tmmode;
    std::string program;
    std::string comment;
    long long part_total = -1;
    bool seen = false;
};

using StateMap = std::map<std::string, MachineState>;

bool is_machining(const MachineState &s)
{
    return s.execution == "ACTIVE" && s.mode == "AUTOMATIC" && s.tmmode == "0";
}

/* ------------------------------------------------------------------ */
/* 流式 XML 增量解析器                                                 */
/* ------------------------------------------------------------------ */

class StreamParser {
public:
    /* 追加一段响应数据；完整可解析的 DeviceStream 块立即处理。
       返回 false 表示调用方应中断读取。 */
    bool feed(const char *data, size_t len, StateMap &states)
    {
        buf_.append(data, len);
        return process(states);
    }
    /* 文档结束（连接关闭）时调用，处理剩余缓冲 */
    void finish(StateMap &states)
    {
        process(states);
        buf_.clear();
    }
    /* 当前已见的最大事件 sequence（用于下一轮 from= 续传） */
    long long last_seq() const { return lastSeq_; }

private:
    std::string buf_;
    long long lastSeq_ = 0;

    /* Header 中的 nextSequence 是权威的续传点：即使响应里只有我们不关心的
       Position/Load 等事件，也能据此推进 from=，避免重复拉取。 */
    void process_header()
    {
        size_t pos = 0;
        for (;;) {
            size_t p = buf_.find("<Header ", pos);
            if (p == std::string::npos) break;
            size_t e = buf_.find('>', p);
            if (e == std::string::npos) break;
            long long ns = parse_ll(attr_of(buf_.substr(p, e - p), "nextSequence").c_str());
            if (ns > lastSeq_) lastSeq_ = ns;
            pos = e + 1;
        }
    }

    void handle_block(const std::string &block, StateMap &states)
    {
        std::string devName = attr_of(block, "name");
        if (devName.empty() || devName == "Agent") return; /* 跳过 agent 自身流 */

        MachineState &st = states[devName];
        if (!st.seen) {
            st.seen = true;
            st.name = devName;
        }

        auto apply = [&](const char *tag,
                         const std::function<void(MachineState &, const std::string &)> &set) {
            size_t pos = 0;
            std::string v;
            long long s = 0;
            while ((pos = next_tag_value(block, pos, tag, v, &s)) != std::string::npos) {
                set(st, trim(v));
                if (s > lastSeq_) lastSeq_ = s;
            }
        };

        apply("Execution", [](MachineState &m, const std::string &v) { m.execution = v; });
        apply("ControllerMode", [](MachineState &m, const std::string &v) { m.mode = v; });
        apply("ProgramComment", [](MachineState &m, const std::string &v) { m.comment = v; });
        apply("Program", [](MachineState &m, const std::string &v) { m.program = v; });
        apply("Availability", [](MachineState &m, const std::string &v) { (void)m; (void)v; });

        std::string v;
        long long s = 0;
        if (named_sample_value(block, "partTotal", v, &s)) {
            st.part_total = parse_ll(v.c_str());
            if (s > lastSeq_) lastSeq_ = s;
        }
        if (named_sample_value(block, "tmMode", v, &s)) {
            st.tmmode = trim(v);
            if (s > lastSeq_) lastSeq_ = s;
        }
    }

    bool process(StateMap &states)
    {
        process_header();
        size_t pos = 0;
        for (;;) {
            size_t p = buf_.find("<DeviceStream ", pos);
            if (p == std::string::npos) break;
            size_t close = buf_.find("</DeviceStream>", p);
            if (close == std::string::npos) break; /* 块未完整，等待更多数据 */
            size_t end = close + 15;
            handle_block(buf_.substr(p, end - p), states);
            buf_.erase(0, end); /* 已处理部分移出缓冲 */
            pos = 0;
        }
        return true;
    }
};

/* 整份文档（/current 或已关闭的 /sample 响应）一次性解析 */
long long parse_document(const std::string &doc, StateMap &states)
{
    StreamParser p;
    p.feed(doc.data(), doc.size(), states);
    p.finish(states);
    return p.last_seq();
}

bool parse_header(const std::string &doc, long long *nextSeq, long long *instanceId)
{
    size_t p = doc.find("<Header ");
    if (p == std::string::npos) return false;
    size_t e = doc.find('>', p);
    if (e == std::string::npos) return false;
    std::string h = doc.substr(p, e - p);
    if (nextSeq) *nextSeq = parse_ll(attr_of(h, "nextSequence").c_str());
    if (instanceId) *instanceId = parse_ll(attr_of(h, "instanceId").c_str());
    return true;
}

/* ------------------------------------------------------------------ */
/* WinHTTP（RAII + 全量 / 流式读取）                                    */
/* ------------------------------------------------------------------ */

class Http {
public:
    Http()
    {
        session_ = WinHttpOpen(L"mtc-stats/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_)
            WinHttpSetTimeouts(session_, 0, 5000, 15000, kReceiveTimeoutMs);
    }
    ~Http() { if (session_) WinHttpCloseHandle(session_); }
    explicit operator bool() const { return session_ != nullptr; }

    /* 全量 GET，返回整份响应体 */
    bool get(int port, const std::string &path, std::string &out)
    {
        return request(port, path,
                       [&](const char *data, size_t len) {
                           out.append(data, len);
                           return true;
                       });
    }

    /* 流式 GET：按块回调；返回 false 表示请求失败（非正常结束） */
    bool stream(int port, const std::string &path,
                const std::function<bool(const char *, size_t)> &onChunk)
    {
        return request(port, path, onChunk);
    }

private:
    HINTERNET session_ = nullptr;

    bool request(int port, const std::string &path,
                 const std::function<bool(const char *, size_t)> &onChunk)
    {
        if (!session_) return false;
        HINTERNET conn = WinHttpConnect(session_, L"127.0.0.1", (INTERNET_PORT)port, 0);
        if (!conn) return false;

        wchar_t wpath[2048];
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, 2048);
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!req) { WinHttpCloseHandle(conn); return false; }

        bool ok = false;
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, NULL)) {
            /* HTTP 错误（如 from 越界 / agent 重启）按请求失败处理，触发重新快照 */
            DWORD status = 0;
            DWORD cb = sizeof(status);
            if (WinHttpQueryHeaders(req,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &cb,
                    WINHTTP_NO_HEADER_INDEX) && status >= 400) {
                ok = false;
            } else {
                ok = true;
            }
        }
        if (ok) {
            char buf[16384];
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) {
                    /* 长连接空闲超时：视为本轮无新数据，正常结束 */
                    if (GetLastError() == ERROR_WINHTTP_TIMEOUT) break;
                    ok = false;
                    break;
                }
                if (avail == 0) break; /* 响应结束 */
                DWORD read = 0;
                if (!WinHttpReadData(req, buf, avail < sizeof(buf) ? avail : (DWORD)sizeof(buf),
                                     &read)) {
                    ok = false;
                    break;
                }
                if (read == 0) break;
                if (!onChunk(buf, read)) { ok = false; break; }
            }
        }

        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        return ok;
    }
};

/* ------------------------------------------------------------------ */
/* SQLite                                                              */
/* ------------------------------------------------------------------ */

class Db {
public:
    explicit Db(const std::string &path)
    {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            fprintf(stderr, "[mtc_stats] cannot open db %s: %s\n", path.c_str(),
                    sqlite3_errmsg(db_));
            db_ = nullptr;
            return;
        }
        sqlite3_exec(db_,
            "CREATE TABLE IF NOT EXISTS samples("
            "  ts INTEGER NOT NULL,"
            "  machine TEXT NOT NULL,"
            "  execution TEXT, mode TEXT, tmmode TEXT,"
            "  program TEXT, comment TEXT,"
            "  part_total INTEGER,"
            "  PRIMARY KEY(ts, machine));"
            "CREATE INDEX IF NOT EXISTS idx_samples_machine ON samples(machine, ts);"
            "CREATE TABLE IF NOT EXISTS stream_state("
            "  key TEXT PRIMARY KEY, value TEXT);",
            NULL, NULL, NULL);

        const char *sql =
            "INSERT OR REPLACE INTO samples(ts,machine,execution,mode,tmmode,"
            "program,comment,part_total) VALUES(?1,?2,?3,?4,?5,?6,?7,?8);";
        sqlite3_prepare_v2(db_, sql, -1, &ins_, nullptr);
    }
    ~Db()
    {
        if (ins_) sqlite3_finalize(ins_);
        if (db_) sqlite3_close(db_);
    }
    explicit operator bool() const { return db_ != nullptr; }
    sqlite3 *handle() { return db_; }

    void flush(const StateMap &states, long long ts)
    {
        if (!ins_) return;
        sqlite3_exec(db_, "BEGIN;", NULL, NULL, NULL);
        for (const auto &kv : states) {
            const MachineState &s = kv.second;
            if (!s.seen) continue;
            sqlite3_bind_int64(ins_, 1, ts);
            sqlite3_bind_text(ins_, 2, s.name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_, 3, s.execution.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_, 4, s.mode.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_, 5, s.tmmode.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_, 6, s.program.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins_, 7, s.comment.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(ins_, 8, s.part_total);
            sqlite3_step(ins_);
            sqlite3_reset(ins_);
            sqlite3_clear_bindings(ins_);
        }
        sqlite3_exec(db_, "COMMIT;", NULL, NULL, NULL);
    }

    std::string get_state(const char *key)
    {
        std::string v;
        sqlite3_stmt *st = nullptr;
        const char *sql = "SELECT value FROM stream_state WHERE key=?1;";
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return v;
        sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW)
            if (auto *p = sqlite3_column_text(st, 0)) v = (const char *)p;
        sqlite3_finalize(st);
        return v;
    }

    void set_state(const char *key, const std::string &v)
    {
        sqlite3_stmt *st = nullptr;
        const char *sql = "INSERT OR REPLACE INTO stream_state(key,value) VALUES(?1,?2);";
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, v.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

private:
    sqlite3 *db_ = nullptr;
    sqlite3_stmt *ins_ = nullptr;
};

/* ------------------------------------------------------------------ */
/* stream / poll                                                       */
/* ------------------------------------------------------------------ */

/* 拉取 /current 全量快照作为基线：重建状态、更新序列号与 instanceId */
bool snapshot(Http &http, Db &db, int port, StateMap &states,
              long long &seq, long long &instanceId)
{
    std::string doc;
    if (!http.get(port, "/current", doc) || doc.empty()) return false;

    StateMap fresh;
    parse_document(doc, fresh);
    long long ns = 0, ii = 0;
    parse_header(doc, &ns, &ii);

    states = std::move(fresh);
    seq = ns;
    if (ii) instanceId = ii;
    db.set_state("seq", std::to_string(seq));
    db.set_state("instance_id", std::to_string(instanceId));
    db.flush(states, (long long)time(nullptr));
    printf("[%lld] snapshot: %zu machines, next_seq=%lld\n",
           (long long)time(nullptr), states.size(), seq);
    return true;
}

int cmd_stream(int port, const std::string &dbpath, int intervalMs)
{
    Db db(dbpath);
    if (!db) return 1;
    Http http;
    if (!http) { fprintf(stderr, "[mtc_stats] WinHttpOpen failed\n"); return 1; }

    long long seq = parse_ll(db.get_state("seq").c_str());
    long long instanceId = parse_ll(db.get_state("instance_id").c_str());
    StateMap states;

    /* 启动时总是先拉一次 /current 快照：重建所有机床基线（含长时间不发事件的
       空闲机床），并用当前 nextSequence 作为增量起点。 */
    if (!snapshot(http, db, port, states, seq, instanceId)) {
        fprintf(stderr, "[mtc_stats] agent not reachable at 127.0.0.1:%d\n", port);
        return 1;
    }

    printf("[mtc_stats] streaming /sample?from=%lld count=%d interval=%dms -> %s\n",
           seq, kSampleCount, intervalMs, dbpath.c_str());

    for (;;) {
        char path[256];
        snprintf(path, sizeof(path), "/sample?from=%lld&count=%d&frequency=1",
                 seq, kSampleCount);

        bool ok = false;
        StreamParser parser;
        if (http.stream(port, path, [&](const char *data, size_t len) {
                return parser.feed(data, len, states);
            })) {
            parser.finish(states);
            ok = true;
        }

        long long now = (long long)time(nullptr);
        if (ok && parser.last_seq() > 0) seq = parser.last_seq() + 1;

        /* 处理错误 / agent 重启：重新快照 */
        if (!ok) {
            printf("[%lld] sample request failed, re-snapshotting\n", now);
            if (!snapshot(http, db, port, states, seq, instanceId))
                printf("[%lld] agent not reachable, retrying in %dms\n", now, intervalMs);
        } else {
            db.set_state("seq", std::to_string(seq));
            db.flush(states, now);
            printf("[%lld] stream seq=%lld machines=%zu\n", now, seq, states.size());
        }

        Sleep(intervalMs < 200 ? 200 : (DWORD)intervalMs);
    }
    return 0;
}

int cmd_poll(int port, int intervalSec, const std::string &dbpath)
{
    Db db(dbpath);
    if (!db) return 1;
    Http http;
    if (!http) { fprintf(stderr, "[mtc_stats] WinHttpOpen failed\n"); return 1; }

    printf("[mtc_stats] polling http://127.0.0.1:%d/current every %ds -> %s\n",
           port, intervalSec, dbpath.c_str());
    for (;;) {
        std::string doc;
        if (http.get(port, "/current", doc) && !doc.empty()) {
            StateMap states;
            parse_document(doc, states);
            long long now = (long long)time(nullptr);
            db.flush(states, now);
            printf("[%lld] captured %zu machines\n", now, states.size());
        } else {
            printf("[mtc_stats] agent not reachable, retrying in %ds\n", intervalSec);
        }
        Sleep(intervalSec * 1000);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* report                                                              */
/* ------------------------------------------------------------------ */

struct SampleRow {
    long long ts = 0;
    std::string machine;
    std::string execution;
    std::string mode;
    std::string tmmode;
    std::string program;
    std::string comment;
    long long part_total = -1;
};

struct ProductStat {
    std::string machine;
    std::string comment;
    long long start_total = -1;
    long long end_total = -1;
};

std::vector<std::string> collect_machines(const std::vector<SampleRow> &rows)
{
    std::vector<std::string> machines;
    for (const auto &r : rows) {
        bool found = false;
        for (const auto &m : machines)
            if (m == r.machine) { found = true; break; }
        if (!found) machines.push_back(r.machine);
    }
    return machines;
}

int cmd_report(const std::string &dbpath, int bucketSec, long long from, long long to)
{
    Db db(dbpath);
    if (!db) return 1;

    sqlite3_stmt *st = nullptr;
    const char *sql =
        "SELECT ts,machine,execution,mode,tmmode,program,comment,part_total "
        "FROM samples WHERE ts>=?1 AND ts<=?2 ORDER BY machine, ts;";
    if (sqlite3_prepare_v2(db.handle(), sql, -1, &st, nullptr) != SQLITE_OK) {
        fprintf(stderr, "[mtc_stats] prepare failed: %s\n", sqlite3_errmsg(db.handle()));
        return 1;
    }
    sqlite3_bind_int64(st, 1, from);
    sqlite3_bind_int64(st, 2, to);

    std::vector<SampleRow> rows;
    while (sqlite3_step(st) == SQLITE_ROW) {
        SampleRow r;
        r.ts = sqlite3_column_int64(st, 0);
        if (auto *p = sqlite3_column_text(st, 1)) r.machine = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 2)) r.execution = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 3)) r.mode = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 4)) r.tmmode = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 5)) r.program = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 6)) r.comment = (const char *)p;
        r.part_total = sqlite3_column_int64(st, 7);
        rows.push_back(std::move(r));
    }
    sqlite3_finalize(st);

    if (rows.empty()) {
        printf("[mtc_stats] no samples in range. Run 'mtc_stats stream/poll' first.\n");
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
    printf("\n=== 加工统计  %s ~ %s  (bucket=%ds) ===\n", fbuf, tbuf, bucketSec);

    /* (1) per-machine running time, bucketed */
    printf("\n[1] 机床实际运行时间 (ACTIVE + AUTOMATIC + tmmode=0, MachSec = count x interval)\n");
    printf("    %-7s %-16s %-10s\n", "Machine", "BucketStart", "MachSec");
    auto machines = collect_machines(rows);
    for (long long b = from / bucketSec * bucketSec; b <= to / bucketSec * bucketSec;
         b += bucketSec) {
        char bts[32];
        time_t bt = (time_t)b;
        struct tm bt2;
        localtime_s(&bt2, &bt);
        strftime(bts, sizeof(bts), "%m-%d %H:%M", &bt2);
        for (const auto &m : machines) {
            long long cnt = 0;
            for (const auto &r : rows) {
                if (r.machine != m) continue;
                if (r.ts < b || r.ts >= b + bucketSec) continue;
                MachineState ms;
                ms.execution = r.execution;
                ms.mode = r.mode;
                ms.tmmode = r.tmmode;
                if (is_machining(ms)) cnt++;
            }
            if (cnt > 0)
                printf("    %-7s %-16s %-10lld\n", m.c_str(), bts, cnt);
        }
    }

    /* (2) per-machine produced parts in range (part_total delta) */
    printf("\n[2] 时间段内产量 (part_total #6712 差值)\n");
    printf("    %-7s %-14s %-14s %-10s\n", "Machine", "StartTotal", "EndTotal", "Produced");
    for (const auto &m : machines) {
        long long first = -1, last = -1;
        for (const auto &r : rows) {
            if (r.machine != m || r.part_total < 0) continue;
            if (first < 0) first = r.part_total;
            last = r.part_total;
        }
        if (first >= 0 && last >= first)
            printf("    %-7s %-14lld %-14lld %-10lld\n",
                   m.c_str(), first, last, last - first);
    }

    /* (3) per-machine per-product parts */
    printf("\n[3] 每种产品产量 (按程序注释分组)\n");
    printf("    %-7s %-22s %-12s\n", "Machine", "Product(comment)", "Produced");
    for (const auto &m : machines) {
        std::vector<ProductStat> ps;
        std::string prevComment;
        ProductStat *cur = nullptr;
        for (const auto &r : rows) {
            if (r.machine != m || r.part_total < 0) continue;
            if (ps.empty() || r.comment != prevComment) {
                ProductStat p;
                p.machine = m;
                p.comment = r.comment;
                ps.push_back(std::move(p));
                cur = &ps.back();
                prevComment = r.comment;
            }
            if (!cur) continue;
            if (cur->start_total < 0) cur->start_total = r.part_total;
            cur->end_total = r.part_total;
        }
        for (const auto &p : ps) {
            long long produced =
                (p.end_total >= p.start_total && p.start_total >= 0)
                    ? p.end_total - p.start_total : 0;
            if (!p.comment.empty() && produced > 0)
                printf("    %-7s %-22s %-12lld\n",
                       m.c_str(), p.comment.c_str(), produced);
        }
    }
    return 0;
}

} // namespace

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8); /* keep UTF-8 output readable on GBK consoles */
    setvbuf(stdout, NULL, _IONBF, 0); /* 重定向到文件时日志也能及时落盘 */
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s stream [http_port] [db] [interval_ms]\n", argv[0]);
        printf("      incremental stream from agent /sample (default port 5000, stats.db, 5000ms)\n");
        printf("  %s poll [http_port] [interval_sec] [db]\n", argv[0]);
        printf("      poll /current snapshot (default port 5000, 5s, stats.db)\n");
        printf("  %s report [db] [bucket_sec] [from_unix] [to_unix]\n", argv[0]);
        printf("      running time / produced parts / per-product parts\n");
        printf("      (default last 24h, 30min buckets)\n");
        return 1;
    }

    if (strcmp(argv[1], "stream") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : kDefaultPort;
        std::string db = argc > 3 ? argv[3] : "stats.db";
        int intervalMs = argc > 4 ? atoi(argv[4]) : kDefaultIntervalMs;
        return cmd_stream(port, db, intervalMs);
    }

    if (strcmp(argv[1], "poll") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : kDefaultPort;
        int interval = argc > 3 ? atoi(argv[3]) : 5;
        std::string db = argc > 4 ? argv[4] : "stats.db";
        return cmd_poll(port, interval, db);
    }

    if (strcmp(argv[1], "report") == 0) {
        std::string db = argc > 2 ? argv[2] : "stats.db";
        int bucket = argc > 3 ? atoi(argv[3]) : 1800;
        long long from, to;
        time_t now = time(nullptr);
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
