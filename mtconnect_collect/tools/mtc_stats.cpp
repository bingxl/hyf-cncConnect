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
 * 统计口径（与 webserver 一致）：
 *   加工中   = execution==ACTIVE && mode==AUTOMATIC（自动模式实际运行时间）
 *   加工时间 = 相邻样本时间差累加（若样本 i 加工中，则累加 ts[i+1]-ts[i]）
 *   产量     = part_total (#6712) 差值
 *   产品     = program comment
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
#include <set>
#include <string>
#include <vector>

#include "db/db.hpp"
#include "db/stats_db.hpp"
#include "config.hpp"

#pragma comment(lib, "winhttp.lib")

namespace {

constexpr int kDefaultPort = 5000;
constexpr int kDefaultIntervalMs = 5000;   /* stream 默认采集间隔 */
constexpr int kSampleCount = 1000;         /* 每次 /sample 最多拉取的事件数 */
/* 以下由 config.json 覆盖（stats.receive_timeout_ms / stats.power_gap_max） */
static int g_receive_timeout_ms = 15000;
static long long g_power_gap_max = 90;
static int g_sample_count = 1000;
static db::Backend g_db_backend = db::Backend::Sqlite;
static std::string g_db_host, g_db_user, g_db_password, g_db_database;
static int g_db_port = 0;

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

std::string fmt_dur(long long sec)
{
    if (sec < 60) return std::to_string(sec) + "s";
    if (sec < 3600) return std::to_string(sec / 60) + "m";
    return std::to_string(sec / 3600) + "h" + std::to_string((sec % 3600) / 60) + "m";
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
    std::string avail; /* AVAILABLE / UNAVAILABLE（开关机信号） */
    std::string program;
    std::string comment;
    long long part_total = -1;
    bool seen = false;

    /* 报警条件：item_id -> (item_type, state)
       state: NORMAL / WARNING / FAULT / FAILED / UNAVAILABLE / ESTOP */
    struct Cond {
        std::string type;
        std::string state;
    };
    std::map<std::string, Cond> conds;
};

using StateMap = std::map<std::string, MachineState>;

/* 相邻样本间隔超过该值视为断联/关机：该时段不计入开机时间与加工时间 */

bool is_machining(const MachineState &s)
{
    return s.execution == "ACTIVE" && s.mode == "AUTOMATIC";
}

static bool row_machining(const std::string &execution, const std::string &mode)
{
    return execution == "ACTIVE" && mode == "AUTOMATIC";
}

/* 报警条件是否处于激活状态（NORMAL/UNAVAILABLE 视为无报警） */
static bool cond_active(const std::string &state)
{
    return state == "WARNING" || state == "FAULT" || state == "FAILED" ||
           state == "ESTOP";
}

/* 解析 <Condition>...</Condition> 段：<Normal|Warning|Fault|Failed|Unavailable
   dataItemId="..." type="..."/> 以及 <EmergencyStop>TRIGGERED</EmergencyStop>。
   增量响应只含变化项，逐项覆盖即可。 */
static void parse_conditions(const std::string &block, MachineState &st)
{
    size_t cs = block.find("<Condition>");
    if (cs == std::string::npos) cs = block.find("<Condition ");
    if (cs == std::string::npos) cs = block.find("<Condition/>");
    if (cs == std::string::npos) cs = block.find("<Condition /");
    size_t ce = block.find("</Condition>", cs == std::string::npos ? 0 : cs);
    if (cs == std::string::npos || ce == std::string::npos) return;
    std::string sec = block.substr(cs, ce - cs);

    static const char *kStates[] = {
        "Normal", "Warning", "Fault", "Failed", "Unavailable"
    };
    for (const char *name : kStates) {
        std::string open = std::string("<") + name;
        size_t pos = 0;
        while ((pos = sec.find(open, pos)) != std::string::npos) {
            char c = (pos + open.size() < sec.size()) ? sec[pos + open.size()] : '\0';
            if (c != ' ' && c != '>' && c != '/') {
                pos += open.size();
                continue;
            }
            size_t gt = sec.find('>', pos);
            if (gt == std::string::npos) break;
            std::string itemId = attr_of(sec.substr(pos, gt - pos), "dataItemId");
            std::string type = attr_of(sec.substr(pos, gt - pos), "type");
            if (!itemId.empty()) {
                std::string stName = name;
                for (auto &ch : stName)
                    ch = (char)toupper((unsigned char)ch);
                st.conds[itemId] = { type.empty() ? "FAULT" : type, stName };
            }
            pos = gt + 1;
            std::string close = "</" + std::string(name) + ">";
            if (sec.compare(pos, close.size(), close) == 0) pos += close.size();
        }
    }

    /* 急停：TRIGGERED 视为报警，ARMED/其他视为正常 */
    {
        std::string v;
        if (next_tag_value(block, 0, "EmergencyStop", v, nullptr) != std::string::npos) {
            std::string s = trim(v);
            if (s == "TRIGGERED")
                st.conds["estop"] = { "ESTOP", "ESTOP" };
            else if (s == "ARMED")
                st.conds["estop"] = { "ESTOP", "NORMAL" };
        }
    }
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
        apply("Availability", [](MachineState &m, const std::string &v) { m.avail = v; });

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

        parse_conditions(block, st);
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
            WinHttpSetTimeouts(session_, 0, 5000, 15000, g_receive_timeout_ms);
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
/* 数据库（抽象层：SQLite 现役，预留 MySQL/PostgreSQL）                  */
/* ------------------------------------------------------------------ */

/* 打开库 + 建表 + 预编译 upsert；失败返回 false 并打印错误 */
static bool open_db(const std::string &path,
                    std::unique_ptr<db::Database> &dbs,
                    std::unique_ptr<db::Statement> &upsert)
{
    db::Config cfg;
    cfg.backend = g_db_backend;
    cfg.file = path;
    cfg.host = g_db_host;
    cfg.port = g_db_port;
    cfg.user = g_db_user;
    cfg.password = g_db_password;
    cfg.database = g_db_database;
    std::string err;
    dbs = db::open(cfg, &err);
    if (!dbs) {
        fprintf(stderr, "[mtc_stats] cannot open db %s: %s\n", path.c_str(), err.c_str());
        return false;
    }
    if (!db::ensure_stats_schema(*dbs, &err)) {
        fprintf(stderr, "[mtc_stats] schema init failed: %s\n", err.c_str());
        return false;
    }
    upsert = db::prepare_sample_upsert(*dbs, &err);
    if (!upsert) {
        fprintf(stderr, "[mtc_stats] prepare upsert failed: %s\n", err.c_str());
        return false;
    }
    return true;
}

/* 批量写入本周期所有机床状态（事务提交） */
static void flush(db::Database &dbs, db::Statement &ins,
                  const StateMap &states, long long ts)
{
    dbs.begin();
    for (const auto &kv : states) {
        const MachineState &s = kv.second;
        if (!s.seen) continue;
        ins.bind_int64(1, ts);
        ins.bind_text(2, s.name);
        ins.bind_text(3, s.execution);
        ins.bind_text(4, s.mode);
        ins.bind_text(5, s.tmmode);
        ins.bind_text(6, s.program);
        ins.bind_text(7, s.comment);
        ins.bind_int64(8, s.part_total);
        /* power=1 开机在线；UNAVAILABLE 视为关机/断联（power=0） */
        ins.bind_int(9, s.avail == "UNAVAILABLE" ? 0 : 1);
        ins.step();
        ins.reset();
    }
    dbs.commit();
}

/* ------------------------------------------------------------------ */
/* 报警同步 + webhook 告警                                              */
/* ------------------------------------------------------------------ */

/* 将当前状态中的条件与 in-memory activeAlarms 对齐：
   新激活项写库（或刷新 last_ts），已消失/恢复正常项关闭（记 end_ts）。 */
static void sync_alarms(db::Database &dbs, const StateMap &states,
                        std::map<std::string, std::set<std::string>> &activeAlarms,
                        long long now)
{
    for (const auto &kv : states) {
        const MachineState &s = kv.second;
        auto &set = activeAlarms[s.name];
        for (const auto &ck : s.conds) {
            if (cond_active(ck.second.state)) {
                db::alarm_upsert(dbs, s.name, ck.first, ck.second.type,
                                 ck.second.state, now);
                set.insert(ck.first);
            }
        }
    }
    auto sit = states.end();
    for (auto &mk : activeAlarms) {
        sit = states.find(mk.first);
        for (auto it = mk.second.begin(); it != mk.second.end();) {
            bool stillActive = false;
            if (sit != states.end()) {
                auto cit = sit->second.conds.find(*it);
                stillActive = cit != sit->second.conds.end() &&
                              cond_active(cit->second.state);
            }
            if (stillActive) {
                ++it;
            } else {
                db::alarm_clear(dbs, mk.first, *it, now);
                it = mk.second.erase(it);
            }
        }
    }
}

/* 启动时恢复未关闭报警，避免进程重启后丢失 first_ts */
static void seed_active_alarms(
    db::Database &dbs, std::map<std::string, std::set<std::string>> &activeAlarms)
{
    auto st = dbs.prepare("SELECT machine, item_id FROM alarms WHERE active=1;");
    if (!st) return;
    while (st->step())
        activeAlarms[st->column_text(0)].insert(st->column_text(1));
}

/* 通用 webhook 通知：POST {"text": "..."} 到 http(s)://host[:port]/path */
class Notifier {
public:
    std::string url;
    long long everySec = 3600; /* 持续异常时重新告警的间隔 */

    bool enabled() const { return !url.empty(); }

    bool send(const std::string &text) const
    {
        if (url.empty()) return false;

        bool secure = false;
        std::string rest = url;
        if (rest.compare(0, 8, "https://") == 0) { secure = true; rest = rest.substr(8); }
        else if (rest.compare(0, 7, "http://") == 0) rest = rest.substr(7);
        else return false;

        size_t slash = rest.find('/');
        std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
        std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
        INTERNET_PORT port = secure ? 443 : 80;
        size_t colon = host.rfind(':');
        if (colon != std::string::npos) {
            port = (INTERNET_PORT)atoi(host.substr(colon + 1).c_str());
            host = host.substr(0, colon);
        }

        HINTERNET hSession = WinHttpOpen(L"mtc-stats-alert/1.0",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        bool ok = false;
        wchar_t whost[256];
        MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, whost, 256);
        HINTERNET hConn = WinHttpConnect(hSession, whost, port, 0);
        if (hConn) {
            wchar_t wpath[1024];
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, 1024);
            HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", wpath, NULL,
                                                WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                secure ? WINHTTP_FLAG_SECURE : 0);
            if (hReq) {
                if (secure) {
                    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                  SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
                    WinHttpSetOption(hReq, WINHTTP_OPTION_SECURITY_FLAGS,
                                     &flags, sizeof(flags));
                }
                std::string body = "{\"text\":\"" + json_escape(text) + "\"}";
                LPCWSTR headers = L"Content-Type: application/json\r\n";
                if (WinHttpSendRequest(hReq, headers, -1L,
                                       (LPVOID)body.c_str(), (DWORD)body.size(),
                                       (DWORD)body.size(), 0))
                    ok = WinHttpReceiveResponse(hReq, NULL);
                WinHttpCloseHandle(hReq);
            }
            WinHttpCloseHandle(hConn);
        }
        WinHttpCloseHandle(hSession);
        return ok;
    }
};

/* 机床离线 / 采集断链检测与通知（只在 Notifier 启用时工作） */
struct AlertTracker {
    std::map<std::string, long long> offlineSince;
    std::map<std::string, long long> lastOfflineAlert;
    bool lastReqOk = true;
    long long lastReqFailAlert = 0;

    void check(const Notifier &n, long long now, const StateMap &states, bool reqOk)
    {
        if (!n.enabled()) return;
        for (const auto &kv : states) {
            const MachineState &s = kv.second;
            bool off = s.avail == "UNAVAILABLE" || s.execution == "UNAVAILABLE";
            if (off) {
                auto it = offlineSince.find(s.name);
                if (it == offlineSince.end()) {
                    offlineSince[s.name] = now;
                    lastOfflineAlert[s.name] = now;
                    n.send(s.name + " 离线（" + s.execution + "/" + s.avail + "）");
                } else if (now - lastOfflineAlert[s.name] >= n.everySec) {
                    lastOfflineAlert[s.name] = now;
                    n.send(s.name + " 仍离线（已持续 " +
                           fmt_dur(now - it->second) + "）");
                }
            } else {
                auto it = offlineSince.find(s.name);
                if (it != offlineSince.end()) {
                    n.send(s.name + " 恢复在线");
                    offlineSince.erase(it);
                    lastOfflineAlert.erase(s.name);
                }
            }
        }
        if (reqOk) {
            if (!lastReqOk) n.send("采集服务恢复（agent 可达）");
            lastReqOk = true;
        } else if (lastReqOk || now - lastReqFailAlert >= n.everySec) {
            lastReqOk = false;
            lastReqFailAlert = now;
            n.send("采集服务不可达（agent 连接失败）");
        }
    }
};

/* ------------------------------------------------------------------ */
/* stream / poll                                                       */
/* ------------------------------------------------------------------ */

/* 拉取 /current 全量快照作为基线：重建状态、更新序列号与 instanceId */
bool snapshot(Http &http, db::Database &dbs, db::Statement &upsert,
              int port, StateMap &states,
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
    db::set_state(dbs, "seq", std::to_string(seq));
    db::set_state(dbs, "instance_id", std::to_string(instanceId));
    flush(dbs, upsert, states, (long long)time(nullptr));
    printf("[%lld] snapshot: %zu machines, next_seq=%lld\n",
           (long long)time(nullptr), states.size(), seq);
    return true;
}

int cmd_stream(int port, const std::string &dbpath, int intervalMs, int pruneDays,
               const std::string &alertUrl, int alertMin)
{
    std::unique_ptr<db::Database> dbs;
    std::unique_ptr<db::Statement> upsert;
    if (!open_db(dbpath, dbs, upsert)) return 1;
    Http http;
    if (!http) { fprintf(stderr, "[mtc_stats] WinHttpOpen failed\n"); return 1; }

    long long seq = parse_ll(db::get_state(*dbs, "seq").c_str());
    long long instanceId = parse_ll(db::get_state(*dbs, "instance_id").c_str());
    StateMap states;
    std::map<std::string, std::set<std::string>> activeAlarms;
    seed_active_alarms(*dbs, activeAlarms);
    Notifier notifier;
    notifier.url = alertUrl;
    notifier.everySec = alertMin > 0 ? (long long)alertMin * 60 : 3600;
    AlertTracker alerts;

    /* 启动时总是先拉一次 /current 快照：重建所有机床基线（含长时间不发事件的
       空闲机床），并用当前 nextSequence 作为增量起点。 */
    if (!snapshot(http, *dbs, *upsert, port, states, seq, instanceId)) {
        fprintf(stderr, "[mtc_stats] agent not reachable at 127.0.0.1:%d\n", port);
        return 1;
    }
    sync_alarms(*dbs, states, activeAlarms, (long long)time(nullptr));

    printf("[mtc_stats] streaming /sample?from=%lld count=%d interval=%dms -> %s\n",
           seq, g_sample_count, intervalMs, dbpath.c_str());
    if (pruneDays > 0)
        printf("[mtc_stats] retention: prune samples older than %d days (hourly)\n",
               pruneDays);
    if (notifier.enabled())
        printf("[mtc_stats] alert webhook: %s (re-alert every %lld min)\n",
               notifier.url.c_str(), notifier.everySec / 60);

    long long lastPrune = 0;
    for (;;) {
        char path[256];
        snprintf(path, sizeof(path), "/sample?from=%lld&count=%d&frequency=1",
                 seq, g_sample_count);

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
            if (!snapshot(http, *dbs, *upsert, port, states, seq, instanceId))
                printf("[%lld] agent not reachable, retrying in %dms\n", now, intervalMs);
            alerts.check(notifier, now, states, false);
        } else {
            db::set_state(*dbs, "seq", std::to_string(seq));
            flush(*dbs, *upsert, states, now);
            sync_alarms(*dbs, states, activeAlarms, now);
            alerts.check(notifier, now, states, true);
            printf("[%lld] stream seq=%lld machines=%zu\n", now, seq, states.size());

            /* 保留策略：每小时清理一次过期采样 */
            if (pruneDays > 0 && now - lastPrune >= 3600) {
                long long cutoff = now - (long long)pruneDays * 86400;
                std::string perr;
                long long removed = db::prune_samples(*dbs, cutoff, &perr);
                if (removed >= 0)
                    printf("[%lld] pruned %lld rows older than %dd\n",
                           now, removed, pruneDays);
                else
                    printf("[%lld] prune failed: %s\n", now, perr.c_str());
                long long arem = db::prune_alarms(*dbs, cutoff, &perr);
                if (arem >= 0 && arem > 0)
                    printf("[%lld] pruned %lld closed alarms\n", now, arem);
                lastPrune = now;
            }
        }

        Sleep(intervalMs < 200 ? 200 : (DWORD)intervalMs);
    }
    return 0;
}

int cmd_poll(int port, int intervalSec, const std::string &dbpath)
{
    std::unique_ptr<db::Database> dbs;
    std::unique_ptr<db::Statement> upsert;
    if (!open_db(dbpath, dbs, upsert)) return 1;
    Http http;
    if (!http) { fprintf(stderr, "[mtc_stats] WinHttpOpen failed\n"); return 1; }
    std::map<std::string, std::set<std::string>> activeAlarms;
    seed_active_alarms(*dbs, activeAlarms);

    printf("[mtc_stats] polling http://127.0.0.1:%d/current every %ds -> %s\n",
           port, intervalSec, dbpath.c_str());
    for (;;) {
        std::string doc;
        if (http.get(port, "/current", doc) && !doc.empty()) {
            StateMap states;
            parse_document(doc, states);
            long long now = (long long)time(nullptr);
            flush(*dbs, *upsert, states, now);
            sync_alarms(*dbs, states, activeAlarms, now);
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
    int power = -1; /* 1=开机在线, 0=关机/断联, -1=旧数据无记录(视为开机) */
};

struct ProductStat {
    std::string machine;
    std::string comment;
    long long start_total = -1;
    long long end_total = -1;
    long long produced = 0;
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
    std::unique_ptr<db::Database> dbs;
    std::unique_ptr<db::Statement> upsert; /* 建表用，report 不使用 */
    if (!open_db(dbpath, dbs, upsert)) return 1;

    const char *sql =
        "SELECT ts,machine,execution,mode,tmmode,program,comment,part_total,power "
        "FROM samples WHERE ts>=?1 AND ts<=?2 ORDER BY machine, ts;";
    auto st = dbs->prepare(sql);
    if (!st) {
        fprintf(stderr, "[mtc_stats] prepare failed: %s\n", dbs->last_error().c_str());
        return 1;
    }
    st->bind_int64(1, from);
    st->bind_int64(2, to);

    std::vector<SampleRow> rows;
    while (st->step()) {
        SampleRow r;
        r.ts = st->column_int64(0);
        r.machine = st->column_text(1);
        r.execution = st->column_text(2);
        r.mode = st->column_text(3);
        r.tmmode = st->column_text(4);
        r.program = st->column_text(5);
        r.comment = st->column_text(6);
        r.part_total = st->column_int64(7);
        r.power = st->column_is_null(8) ? -1 : st->column_int(8);
        rows.push_back(std::move(r));
    }

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

    /* (1) per-machine running time, bucketed (相邻样本时间差累加) */
    printf("\n[1] 机床运行统计 (加工时间/开机时间/利用率, 相邻样本时间差累加)\n");
    printf("    %-7s %-16s %-10s %-10s %-8s %-10s\n",
           "Machine", "BucketStart", "MachSec", "PowerSec", "Util", "MachCnt");
    auto machines = collect_machines(rows);
    for (const auto &m : machines) {
        std::map<long long, long long> bucket_secs;
        std::map<long long, long long> bucket_power;
        std::map<long long, long long> bucket_cnt;
        const SampleRow *prev = nullptr;
        bool prev_mach = false;
        bool prev_power = false;
        for (const auto &r : rows) {
            if (r.machine != m) continue;
            bool pow = r.power != 0; /* NULL/旧数据视为开机 */
            bool mach = pow && row_machining(r.execution, r.mode);
            if (mach) {
                long long b = (r.ts / bucketSec) * bucketSec;
                bucket_cnt[b]++;
            }
            if (prev && r.ts > prev->ts && r.ts - prev->ts <= g_power_gap_max) {
                long long t0 = prev->ts, t1 = r.ts;
                for (long long b = (t0 / bucketSec) * bucketSec; b < t1; b += bucketSec) {
                    long long lo = b > t0 ? b : t0;
                    long long hi = (b + bucketSec < t1) ? b + bucketSec : t1;
                    if (hi <= lo) continue;
                    if (prev_mach) bucket_secs[b] += hi - lo;
                    if (prev_power) bucket_power[b] += hi - lo;
                }
            }
            prev = &r;
            prev_mach = mach;
            prev_power = pow;
        }
        for (const auto &kv : bucket_secs) {
            if (kv.second <= 0 && bucket_power[kv.first] <= 0) continue;
            char bts[32];
            time_t bt = (time_t)kv.first;
            struct tm bt2;
            localtime_s(&bt2, &bt);
            strftime(bts, sizeof(bts), "%m-%d %H:%M", &bt2);
            long long pw = bucket_power[kv.first];
            printf("    %-7s %-16s %-10lld %-10lld %6d%% %-10lld\n",
                   m.c_str(), bts, kv.second, pw,
                   pw > 0 ? (int)(kv.second * 100 / pw) : 0,
                   bucket_cnt[kv.first]);
        }
    }

    /* (2) per-machine produced parts in range (part_total 正增量求和,
           兼容计数清零/重启) */
    printf("\n[2] 时间段内产量 (part_total 正增量求和)\n");
    printf("    %-7s %-14s %-14s %-10s\n", "Machine", "StartTotal", "EndTotal", "Produced");
    for (const auto &m : machines) {
        long long first = -1, last = -1, produced = 0;
        const SampleRow *prev = nullptr;
        for (const auto &r : rows) {
            if (r.machine != m) continue;
            if (r.part_total < 0) continue;
            if (first < 0) first = r.part_total;
            last = r.part_total;
            if (prev && prev->part_total >= 0 && r.part_total > prev->part_total)
                produced += r.part_total - prev->part_total;
            prev = &r;
        }
        if (first >= 0)
            printf("    %-7s %-14lld %-14lld %-10lld\n",
                   m.c_str(), first, last, produced);
    }

    /* (3) per-machine per-product parts */
    printf("\n[3] 每种产品产量 (按程序注释分组)\n");
    printf("    %-7s %-22s %-12s\n", "Machine", "Product(comment)", "Produced");
    for (const auto &m : machines) {
        std::vector<ProductStat> ps;
        std::string prevComment;
        const SampleRow *prev = nullptr;
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
            if (prev && prev->part_total >= 0 && r.part_total > prev->part_total)
                cur->produced += r.part_total - prev->part_total;
            prev = &r;
        }
        for (const auto &p : ps) {
            if (!p.comment.empty() && p.produced > 0)
                printf("    %-7s %-22s %-12lld\n",
                       m.c_str(), p.comment.c_str(), p.produced);
        }
    }
    return 0;
}

/* 保留策略：删除 db 中早于 retention_days 的采样 */
int cmd_prune(const std::string &dbpath, long long retentionDays)
{
    std::unique_ptr<db::Database> dbs;
    std::unique_ptr<db::Statement> upsert;
    if (!open_db(dbpath, dbs, upsert)) return 1;

    long long now = (long long)time(nullptr);
    long long cutoff = now - retentionDays * 86400;
    std::string err;
    long long removed = db::prune_samples(*dbs, cutoff, &err);
    if (removed < 0) {
        fprintf(stderr, "[mtc_stats] prune failed: %s\n", err.c_str());
        return 1;
    }
    printf("[mtc_stats] pruned %lld rows older than %lld days\n", removed, retentionDays);
    long long arem = db::prune_alarms(*dbs, cutoff, &err);
    if (arem >= 0)
        printf("[mtc_stats] pruned %lld closed alarms\n", arem);
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

    cfg::Config c;
    std::string cerr;
    cfg::load(c, "", &cerr);
    if (!cerr.empty()) fprintf(stderr, "[mtc_stats] %s\n", cerr.c_str());
    g_receive_timeout_ms = c.receive_timeout_ms;
    g_power_gap_max = c.power_gap_max;
    g_sample_count = c.sample_count;
    g_db_backend = c.db_type == "mysql" ? db::Backend::Mysql
                   : c.db_type == "postgres" ? db::Backend::Postgres
                                             : db::Backend::Sqlite;
    g_db_host = c.db_host;
    g_db_port = c.db_port;
    g_db_user = c.db_user;
    g_db_password = c.db_password;
    g_db_database = c.db_database;

    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s stream [http_port] [db] [interval_ms] [prune_days] [alert_url] [alert_min]\n", argv[0]);
        printf("      incremental stream from agent /sample\n");
        printf("      (default port 5000, stats.db, 5000ms; prune_days>0 每小时清理过期数据;\n");
        printf("       alert_url 非空时按 webhook 通知机床离线/采集中断, alert_min 为重复告警间隔)\n");
        printf("  %s poll [http_port] [interval_sec] [db]\n", argv[0]);
        printf("      poll /current snapshot (default port 5000, 5s, stats.db)\n");
        printf("  %s report [db] [bucket_sec] [from_unix] [to_unix]\n", argv[0]);
        printf("      running time / produced parts / per-product parts\n");
        printf("      (default last 24h, 30min buckets)\n");
        printf("  %s prune [db] [retention_days]\n", argv[0]);
        printf("      delete samples older than retention_days (default stats.db, 90d)\n");
        return 1;
    }

    if (strcmp(argv[1], "stream") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : c.agent_http_port;
        std::string db = argc > 3 ? argv[3] : c.db_path;
        int intervalMs = argc > 4 ? atoi(argv[4]) : c.stream_interval_ms;
        int pruneDays = argc > 5 ? atoi(argv[5]) : c.retention_days;
        std::string alertUrl = argc > 6 ? argv[6] : c.alert_url;
        if (alertUrl == "-") alertUrl.clear();
        int alertMin = argc > 7 ? atoi(argv[7]) : c.alert_min;
        return cmd_stream(port, db, intervalMs, pruneDays, alertUrl, alertMin);
    }

    if (strcmp(argv[1], "poll") == 0) {
        int port = argc > 2 ? atoi(argv[2]) : c.agent_http_port;
        int interval = argc > 3 ? atoi(argv[3]) : c.poll_interval_sec;
        std::string db = argc > 4 ? argv[4] : c.db_path;
        return cmd_poll(port, interval, db);
    }

    if (strcmp(argv[1], "report") == 0) {
        std::string db = argc > 2 ? argv[2] : c.db_path;
        int bucket = argc > 3 ? atoi(argv[3]) : c.web_default_bucket_sec;
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

    if (strcmp(argv[1], "prune") == 0) {
        std::string db = argc > 2 ? argv[2] : c.db_path;
        long long days = argc > 3 ? _atoi64(argv[3]) : c.retention_days;
        return cmd_prune(db, days);
    }

    printf("unknown command '%s'\n", argv[1]);
    return 1;
}
