/*
 * config.cpp - config.json 加载 / 默认值写入（极小 JSON 解析器）
 */
#include "config.hpp"

#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <map>
#include <vector>

namespace cfg {
namespace {

bool file_exists(const std::string &p)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

bool read_file(const std::string &p, std::string &out)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    if (n > 0 && fread(&out[0], 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

bool write_file(const std::string &p, const std::string &text)
{
    FILE *f = fopen(p.c_str(), "w");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    return ok;
}

std::string user_home()
{
    const char *h = getenv("USERPROFILE");
    if (h && *h) return h;
    h = getenv("HOME");
    if (h && *h) return h;
    return "";
}

/* ---------------- 极小 JSON 解析器：叶子键 -> "a.b.c" 扁平 map ---------------- */
struct JsonParser {
    const char *p;
    std::map<std::string, std::string> &out;
    bool bad = false;

    explicit JsonParser(const std::string &text, std::map<std::string, std::string> &o)
        : p(text.c_str()), out(o) {}

    void skip_ws()
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    }

    std::string parse_string()
    {
        if (*p != '"') { bad = true; return ""; }
        p++;
        std::string s;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'r': s += '\r'; break;
                case 'u': /* 简单跳过 \uXXXX */
                    for (int i = 0; i < 4 && p[1]; i++) p++;
                    break;
                default: s += *p;
                }
            } else {
                s += *p;
            }
            p++;
        }
        if (*p == '"') p++;
        return s;
    }

    void parse_value(const std::string &prefix)
    {
        skip_ws();
        if (*p == '{') {
            p++;
            parse_object(prefix);
        } else if (*p == '[') {
            /* 数组暂不支持：整体跳过 */
            p++;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                p++;
            }
        } else if (*p == '"') {
            out[prefix] = parse_string();
        } else {
            const char *s = p;
            while (*p && *p != ',' && *p != '}' && *p != ']' && *p != '\n' &&
                   *p != '\r')
                p++;
            std::string v(s, (size_t)(p - s));
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
                v.erase(v.begin());
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
                v.pop_back();
            out[prefix] = v;
        }
    }

    void parse_object(const std::string &prefix)
    {
        skip_ws();
        if (*p == '}') { p++; return; }
        for (;;) {
            skip_ws();
            if (*p != '"') { bad = true; return; }
            std::string key = parse_string();
            skip_ws();
            if (*p != ':') { bad = true; return; }
            p++;
            std::string path = prefix.empty() ? key : prefix + "." + key;
            parse_value(path);
            skip_ws();
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; return; }
            bad = true;
            return;
        }
    }

    bool run()
    {
        skip_ws();
        if (*p == '{') { p++; parse_object(""); }
        else bad = true;
        return !bad;
    }
};

std::string get_str(const std::map<std::string, std::string> &kv,
                    const char *k, const std::string &def)
{
    auto it = kv.find(k);
    return it == kv.end() ? def : it->second;
}

long long get_ll(const std::map<std::string, std::string> &kv,
                 const char *k, long long def)
{
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    return _strtoi64(it->second.c_str(), nullptr, 10);
}

int get_int(const std::map<std::string, std::string> &kv, const char *k, int def)
{
    return (int)get_ll(kv, k, def);
}

bool get_bool(const std::map<std::string, std::string> &kv, const char *k, bool def)
{
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    std::string v = it->second;
    for (auto &ch : v) ch = (char)tolower((unsigned char)ch);
    return v == "true" || v == "1" || v == "yes" || v == "on";
}

void fill(Config &c, const std::map<std::string, std::string> &kv)
{
    c.agent_http_port = get_int(kv, "agent.http_port", c.agent_http_port);
    c.shdr_base_port = get_int(kv, "agent.shdr_base_port", c.shdr_base_port);
    c.agent_dir = get_str(kv, "agent.agent_dir", c.agent_dir);
    c.devices_dir = get_str(kv, "agent.devices_dir", c.devices_dir);
    c.monitor_config_files = get_bool(kv, "agent.monitor_config_files", c.monitor_config_files);
    c.agent_buffer_size = get_int(kv, "agent.buffer_size", c.agent_buffer_size);

    c.db_type = get_str(kv, "db.type", c.db_type);
    c.db_path = get_str(kv, "db.path", c.db_path);
    c.db_host = get_str(kv, "db.host", c.db_host);
    c.db_port = get_int(kv, "db.port", c.db_port);
    c.db_user = get_str(kv, "db.user", c.db_user);
    c.db_password = get_str(kv, "db.password", c.db_password);
    c.db_database = get_str(kv, "db.database", c.db_database);

    c.stream_interval_ms = get_int(kv, "stats.stream_interval_ms", c.stream_interval_ms);
    c.poll_interval_sec = get_int(kv, "stats.poll_interval_sec", c.poll_interval_sec);
    c.sample_count = get_int(kv, "stats.sample_count", c.sample_count);
    c.receive_timeout_ms = get_int(kv, "stats.receive_timeout_ms", c.receive_timeout_ms);
    c.power_gap_max = get_ll(kv, "stats.power_gap_max", c.power_gap_max);
    c.retention_days = get_int(kv, "stats.retention_days", c.retention_days);
    c.prune_interval_sec = get_ll(kv, "stats.prune_interval_sec", c.prune_interval_sec);
    c.alert_url = get_str(kv, "stats.alert_url", c.alert_url);
    c.alert_min = get_int(kv, "stats.alert_min", c.alert_min);

    c.web_port = get_int(kv, "web.port", c.web_port);
    c.web_root = get_str(kv, "web.web_root", c.web_root);
    c.web_agent_port = get_int(kv, "web.agent_port", c.web_agent_port);
    c.web_max_rows = (std::size_t)get_ll(kv, "web.max_rows", (long long)c.web_max_rows);
    c.web_default_bucket_sec = get_int(kv, "web.default_bucket_sec", c.web_default_bucket_sec);

    c.fanuc_connect_timeout_sec = get_int(kv, "fanuc.connect_timeout_sec",
                                          c.fanuc_connect_timeout_sec);
    c.fanuc_reconnect_wait_ms = get_int(kv, "fanuc.reconnect_wait_ms",
                                        c.fanuc_reconnect_wait_ms);
    c.fanuc_scan_delay_ms = get_int(kv, "fanuc.scan_delay_ms", c.fanuc_scan_delay_ms);
    c.fanuc_comment_retry_ms = get_int(kv, "fanuc.comment_retry_ms",
                                       c.fanuc_comment_retry_ms);

    c.mazak_scan_delay_ms = get_int(kv, "mazak.scan_delay_ms", c.mazak_scan_delay_ms);
    c.mazak_socket_timeout_ms = get_int(kv, "mazak.socket_timeout_ms",
                                        c.mazak_socket_timeout_ms);

    c.log_dir = get_str(kv, "log.dir", c.log_dir);
}

} // namespace

static const char *kDefaultConfig =
    "{\n"
    "  \"agent\": {\n"
    "    \"http_port\": 5000,\n"
    "    \"shdr_base_port\": 7878,\n"
    "    \"agent_dir\": \"agent\",\n"
    "    \"devices_dir\": \"devices\",\n"
    "    \"monitor_config_files\": false,\n"
    "    \"buffer_size\": 17\n"
    "  },\n"
    "  \"db\": {\n"
    "    \"type\": \"sqlite\",\n"
    "    \"path\": \"stats.db\",\n"
    "    \"host\": \"\",\n"
    "    \"port\": 0,\n"
    "    \"user\": \"\",\n"
    "    \"password\": \"\",\n"
    "    \"database\": \"\"\n"
    "  },\n"
    "  \"stats\": {\n"
    "    \"stream_interval_ms\": 5000,\n"
    "    \"poll_interval_sec\": 5,\n"
    "    \"sample_count\": 1000,\n"
    "    \"receive_timeout_ms\": 15000,\n"
    "    \"power_gap_max\": 90,\n"
    "    \"retention_days\": 90,\n"
    "    \"prune_interval_sec\": 3600,\n"
    "    \"alert_url\": \"\",\n"
    "    \"alert_min\": 60\n"
    "  },\n"
    "  \"web\": {\n"
    "    \"port\": 8088,\n"
    "    \"web_root\": \"web/dist\",\n"
    "    \"agent_port\": 5000,\n"
    "    \"max_rows\": 1000000,\n"
    "    \"default_bucket_sec\": 1800\n"
    "  },\n"
    "  \"fanuc\": {\n"
    "    \"connect_timeout_sec\": 10,\n"
    "    \"reconnect_wait_ms\": 5000,\n"
    "    \"scan_delay_ms\": 100,\n"
    "    \"comment_retry_ms\": 5000\n"
    "  },\n"
    "  \"mazak\": {\n"
    "    \"scan_delay_ms\": 100,\n"
    "    \"socket_timeout_ms\": 3000\n"
    "  },\n"
    "  \"log\": {\n"
    "    \"dir\": \"log\"\n"
    "  }\n"
    "}\n";

bool save_default(const std::string &path, std::string *err)
{
    if (!write_file(path, kDefaultConfig)) {
        if (err) *err = "cannot write default config: " + path;
        return false;
    }
    return true;
}

bool load(Config &c, const std::string &rootHint, std::string *err)
{
    std::vector<std::string> candidates;
    if (!rootHint.empty()) candidates.push_back(rootHint + "\\config.json");
    candidates.push_back("config.json");
    std::string home = user_home();
    if (!home.empty()) candidates.push_back(home + "\\mtconnect\\config.json");

    std::string path;
    for (const auto &p : candidates)
        if (file_exists(p)) { path = p; break; }

    if (path.empty()) {
        /* 找不到：使用默认值，并把默认配置写入 config.json（优先项目根） */
        std::string write = rootHint.empty() ? "config.json" : rootHint + "\\config.json";
        if (!save_default(write, err)) {
            if (!home.empty()) {
                std::string dir = home + "\\mtconnect";
#ifdef _WIN32
                CreateDirectoryA(dir.c_str(), nullptr);
#else
                mkdir(dir.c_str(), 0755);
#endif
                write = dir + "\\config.json";
                save_default(write, err);
            }
        }
        c.config_path.clear();
        return true;
    }

    std::string text;
    if (!read_file(path, text)) {
        if (err) *err = "cannot read config: " + path;
        return false;
    }
    std::map<std::string, std::string> kv;
    JsonParser parser(text, kv);
    if (!parser.run()) {
        if (err) *err = "config json parse error: " + path;
        return false;
    }
    fill(c, kv);
    c.config_path = path;
    return true;
}

} // namespace cfg
