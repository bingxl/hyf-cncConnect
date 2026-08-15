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
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <cstdarg>

#include "sqlite3.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {

const size_t MAX_HTTP = 65536;
const size_t MAX_ROWS = 1000000;
const int SAMPLE_INTERVAL_DEFAULT = 10;

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
};

struct SampleSet {
    std::vector<Sample> rows;
    long long first_ts = 0, last_ts = 0;
    int interval_sec = SAMPLE_INTERVAL_DEFAULT;
};

bool is_machining(const Sample &s)
{
    if (s.execution != "ACTIVE") return false;
    if (s.mode != "AUTOMATIC") return false;
    if (!s.tmmode.empty() && s.tmmode != "0") return false;
    return true;
}

bool load_samples(sqlite3 *db, long long from, long long to,
                  const std::string &machine, SampleSet *ss)
{
    static const char *sqlAll =
        "SELECT ts,machine,execution,mode,tmmode,comment,part_total "
        "FROM samples WHERE ts>=?1 AND ts<=?2 ORDER BY ts;";
    static const char *sqlOne =
        "SELECT ts,machine,execution,mode,tmmode,comment,part_total "
        "FROM samples WHERE ts>=?1 AND ts<=?2 AND machine=?3 ORDER BY ts;";
    const char *sql = machine.empty() ? sqlAll : sqlOne;

    ss->rows.clear();
    ss->rows.reserve(MAX_ROWS);
    ss->first_ts = ss->last_ts = 0;
    ss->interval_sec = SAMPLE_INTERVAL_DEFAULT;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, from);
    sqlite3_bind_int64(st, 2, to);
    if (!machine.empty()) sqlite3_bind_text(st, 3, machine.c_str(), -1, SQLITE_STATIC);

    while (ss->rows.size() < MAX_ROWS && sqlite3_step(st) == SQLITE_ROW) {
        Sample r;
        r.ts = sqlite3_column_int64(st, 0);
        if (auto *p = sqlite3_column_text(st, 1)) r.machine = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 2)) r.execution = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 3)) r.mode = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 4)) r.tmmode = (const char *)p;
        if (auto *p = sqlite3_column_text(st, 5)) r.comment = (const char *)p;
        r.part_total = sqlite3_column_int64(st, 6);
        ss->rows.push_back(std::move(r));
    }
    sqlite3_finalize(st);

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
        long long mach_sec = 0, cnt = 0, sample_cnt = 0, mfirst = 0, mlast = 0;
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
        long long mins = SAMPLE_INTERVAL_DEFAULT;
        if (sample_cnt > 1 && mlast > mfirst) {
            long long d = (mlast - mfirst) / (sample_cnt - 1);
            if (d >= 1 && d <= 3600) mins = d;
        }
        mach_sec = cnt * mins;
        long long produced = (first_total >= 0 && last_total >= first_total) ? last_total - first_total : 0;
        double util = (mlast > mfirst) ? (double)mach_sec / (double)(mlast - mfirst) : 0.0;

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
            "{\"machine\":\"%s\",\"mach_sec\":%lld,\"util_rate\":%.3f,"
            "\"part_total_start\":%lld,\"part_total_end\":%lld,\"produced\":%lld,"
            "\"sample_count\":%lld,\"machining_count\":%lld,"
            "\"last\":{\"execution\":\"%s\",\"mode\":\"%s\",\"comment\":\"%s\",\"ts\":%lld}}",
            name.c_str(), mach_sec, util, first_total, last_total, produced,
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
    bool first = true;
    for (long long b = bstart; b <= bend; b += bucket) {
        long long cnt = 0, scnt = 0;
        for (const auto &s : ss.rows) {
            if (s.ts < b || s.ts >= b + bucket) continue;
            scnt++;
            if (is_machining(s)) cnt++;
        }
        if (scnt > 0) {
            if (!first) o += ",";
            first = false;
            fmt(o, "{\"bucket_ts\":%lld,\"mach_sec\":%lld,\"machining_count\":%lld,\"sample_count\":%lld}",
                b, cnt * ss.interval_sec, cnt, scnt);
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
    bool first = true;
    for (long long b = bstart; b <= bend; b += bucket) {
        long long st = -1, en = -1;
        for (const auto &s : ss.rows) {
            if (s.ts < b || s.ts >= b + bucket) continue;
            if (s.part_total < 0) continue;
            if (st < 0) st = s.part_total;
            en = s.part_total;
        }
        if (st >= 0) {
            long long prod = (en >= st) ? en - st : 0;
            if (!first) o += ",";
            first = false;
            fmt(o, "{\"bucket_ts\":%lld,\"produced\":%lld,\"start_total\":%lld,\"end_total\":%lld}",
                b, prod, st, en);
        }
    }
    o += "]}";
    return o;
}

std::string api_products(const SampleSet &ss, const std::string &machine)
{
    struct Prod {
        std::string comment;
        long long start_total = -1, end_total = -1, mach_sec = 0, first_ts = 0, last_ts = 0;
        bool have = false;
    };
    std::vector<Prod> list;

    for (const auto &s : ss.rows) {
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
        if (is_machining(s)) cur->mach_sec += ss.interval_sec;
        if (!cur->first_ts) cur->first_ts = s.ts;
        cur->last_ts = s.ts;
        cur->have = true;
    }

    std::string o;
    fmt(o, "{\"machine\":\"%s\",\"items\":[", machine.c_str());
    bool first = true;
    for (const auto &p : list) {
        if (!p.have) continue;
        long long produced = (p.start_total >= 0 && p.end_total >= p.start_total)
            ? p.end_total - p.start_total : 0;
        if (!first) o += ",";
        first = false;
        fmt(o,
            "{\"comment\":\"%s\",\"produced\":%lld,\"mach_sec\":%lld,"
            "\"start_total\":%lld,\"end_total\":%lld,\"first_ts\":%lld,\"last_ts\":%lld}",
            json_escape(p.comment).c_str(), produced, p.mach_sec,
            p.start_total, p.end_total, p.first_ts, p.last_ts);
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
    sqlite3 *db = nullptr;
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

    if (path == "/api/health") {
        long long rows = 0, first = 0, last = 0;
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(cfg.db, "SELECT COUNT(*), MIN(ts), MAX(ts) FROM samples;",
                               -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                rows = sqlite3_column_int64(st, 0);
                first = sqlite3_column_int64(st, 1);
                last = sqlite3_column_int64(st, 2);
            }
            sqlite3_finalize(st);
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
        sqlite3_stmt *st = nullptr;
        bool first = true;
        if (sqlite3_prepare_v2(cfg.db,
                "SELECT machine, MIN(ts), MAX(ts) FROM samples GROUP BY machine ORDER BY machine;",
                -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char *m = (const char *)sqlite3_column_text(st, 0);
                if (!first) o += ",";
                first = false;
                fmt(o, "{\"name\":\"%s\",\"first_ts\":%lld,\"last_ts\":%lld}",
                    m, sqlite3_column_int64(st, 1), sqlite3_column_int64(st, 2));
            }
            sqlite3_finalize(st);
        }
        o += "]}";
        r.send_json(200, o);
        return;
    }

    if (path.compare(0, 11, "/api/stats/") == 0) {
        long long from = q.get_ll("from", (long long)time(nullptr) - 86400);
        long long to = q.get_ll("to", (long long)time(nullptr));
        int bucket = q.get_int("bucket", 1800);
        std::string machine;
        if (const std::string *v = q.get("machine")) machine = *v;
        if (bucket < 60) bucket = 60;

        SampleSet ss;
        if (!load_samples(cfg.db, from, to, machine, &ss)) {
            r.send_error(500, "db query failed");
            return;
        }
        if (ss.rows.empty()) {
            r.send_json(200, "{\"items\":[]}");
            return;
        }

        std::string out;
        if (path == "/api/stats/summary") {
            out = api_summary(ss);
        } else if (path == "/api/stats/machining") {
            out = api_machining(ss, bucket, machine.empty() ? ss.rows.front().machine : machine);
        } else if (path == "/api/stats/production") {
            out = api_production(ss, bucket, machine.empty() ? ss.rows.front().machine : machine);
        } else if (path == "/api/stats/products") {
            out = api_products(ss, machine.empty() ? ss.rows.front().machine : machine);
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
    int port = argc > 1 ? atoi(argv[1]) : 8088;
    const char *dbpath = argc > 2 ? argv[2] : "stats.db";
    ServerConfig cfg;
    cfg.agent_port = argc > 3 ? atoi(argv[3]) : 5000;
    cfg.web_root = argc > 4 ? argv[4] : "web/dist";

    if (sqlite3_open(dbpath, &cfg.db) != SQLITE_OK) {
        fprintf(stderr, "cannot open %s: %s\n", dbpath, sqlite3_errmsg(cfg.db));
        return 1;
    }

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
           port, dbpath, cfg.agent_port, cfg.web_root.c_str());

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
    sqlite3_close(cfg.db);
    WSACleanup();
    return 0;
}
