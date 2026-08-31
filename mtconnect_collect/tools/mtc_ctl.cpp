/*
 * mtc_ctl.cpp - MTConnect 采集服务控制台（替代 start/stop/status/test/view
 *               /start_poll/start_web/start_hidden/run_all .bat 与 status.ps1）
 *
 * Usage:
 *   mtc_ctl start [http_port] [shdr_base] [--no-poll] [--no-web] [--root DIR]
 *                 [--jichuang PATH] [--agent-dir DIR] [--visible]
 *      启动：genconfig -> 各机床采集器 -> agent -> poll(stream) -> webserver
 *   mtc_ctl test [http_port] [shdr_base] [--root DIR]
 *      全 SIM 演示：所有机台启动 cnc_sim，等待 6s 后打印状态表
 *   mtc_ctl stop [--root DIR]
 *      停止 root 下由本工具启动的 agent/采集器/模拟器/统计/Web 进程
 *   mtc_ctl status [http_port] [--root DIR] [--web-port N]
 *      进程状态 + agent 机床连接表 + webserver 健康检查
 *   mtc_ctl poll [--http-port N] [--db PATH] [--interval-ms N] [--prune-days N]
 *                [--alert-url URL] [--alert-min N] [--root DIR]
 *      后台启动统计采集（mtc_stats stream）
 *   mtc_ctl web [--port N] [--db PATH] [--agent-port N] [--web-root PATH] [--root DIR]
 *      后台启动 Web 服务（webserver）
 *   mtc_ctl report [bucket] [from] [to] [--db PATH] [--root DIR]
 *      运行统计报表（mtc_stats report）
 *   mtc_ctl install [--http-port N] [--shdr-base N] [--web-port N] [--root DIR]
 *      安装 Windows 服务（服务二进制即 mtc_ctl.exe 本身，开机自启）
 *   mtc_ctl uninstall
 *      删除已安装的 Windows 服务
 *
 * Windows 服务说明:
 *   - 服务 ImagePath 指向 mtc_ctl.exe 本身 + 专用命令字 `service` 及运行参数，
 *     例如 `"C:\...\mtc_ctl.exe" service --root "C:\..." --http-port 5000`。
 *   - 因此 SCM 启动时走的是独立命令字 `service`，与交互式 CLI 的
 *     start/stop/status/... 互不冲突，化解“服务事件传参”与“本程序已有
 *     命令行参数”之间的冲突。`service` 命令只能在 SCM 环境中运行。
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "config.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

/* ------------------------------------------------------------------ */
/* 字符串 / 路径                                                       */
/* ------------------------------------------------------------------ */

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

std::vector<std::string> split_ws(const std::string &s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '\r' && s[j] != '\n') j++;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string lower_str(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return s;
}

std::wstring utf8_to_wide(const std::string &s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string wide_to_utf8(const std::wstring &w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n ? n - 1 : 0, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring normalize_w(const std::wstring &p)
{
    std::wstring s = p;
    for (auto &c : s)
        if (c == L'/') c = L'\\';
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    std::wstring lo = s;
    for (auto &c : lo) c = (wchar_t)towlower(c);
    return lo;
}

bool path_under_root(const std::wstring &path, const std::wstring &root)
{
    std::wstring p = normalize_w(path);
    std::wstring r = normalize_w(root);
    return p.size() >= r.size() && p.compare(0, r.size(), r) == 0 &&
           (p.size() == r.size() || p[r.size()] == L'\\');
}

/* 当前 exe 所在目录（bin\） */
std::string exe_dir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p = p.substr(0, slash);
    return wide_to_utf8(p);
}

/* 默认项目根 = exe 目录的上一级 */
std::string default_root()
{
    std::string d = exe_dir();
    size_t slash = d.find_last_of("\\/");
    return slash == std::string::npos ? d : d.substr(0, slash);
}

std::string join_path(const std::string &a, const std::string &b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a.back() == '\\' || a.back() == '/' ? a + b : a + "\\" + b;
}

/* ------------------------------------------------------------------ */
/* 进程控制                                                            */
/* ------------------------------------------------------------------ */

static const wchar_t *kServiceNames[] = {
    L"agent.exe", L"fanuc_adapter.exe", L"mazak_adapter.exe",
    L"cnc_sim.exe", L"cnc_sim_ctl.exe", L"shdr_sim.exe", L"mazak_sim.exe",
    L"mtc_stats.exe", L"webserver.exe"
};

bool is_service_name(const std::wstring &name)
{
    std::wstring n = name;
    for (auto &c : n) c = (wchar_t)towlower(c);
    for (auto *s : kServiceNames)
        if (n == s) return true;
    return false;
}

/* root 下运行的服务进程：name(小写) -> PID 列表 */
std::map<std::wstring, std::vector<DWORD>> running_services(const std::wstring &root)
{
    std::map<std::wstring, std::vector<DWORD>> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (!is_service_name(pe.szExeFile)) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        wchar_t path[MAX_PATH];
        DWORD sz = MAX_PATH;
        if (h && QueryFullProcessImageNameW(h, 0, path, &sz)) {
            if (path_under_root(path, root)) {
                std::wstring n = pe.szExeFile;
                for (auto &c : n) c = (wchar_t)towlower(c);
                out[n].push_back(pe.th32ProcessID);
            }
        }
        if (h) CloseHandle(h);
    }
    CloseHandle(snap);
    return out;
}

/* 优雅停止 root 下的服务进程：
 *   阶段1：向各进程的控制台发送 CTRL_BREAK（请求优雅退出；装了信号处理器的
 *           子进程如 cnc_sim 会走清理路径，其余控制台程序按默认 SIGBREAK 退出）。
 *   阶段2：最多等 graceMs 毫秒，让进程自行落盘/退出（整体等待被封顶到 graceMs）。
 *   阶段3：仍存活者强制结束（TerminateProcess），保证 stop 不会卡死。
 *   graceMs=0 表示不发送信号、不等待，直接强制结束。 */
void stop_services(const std::wstring &root, DWORD graceMs = 5000)
{
    /* 收集 root 下待停止的进程 PID */
    std::vector<DWORD> pids;
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            if (!is_service_name(pe.szExeFile)) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            wchar_t path[MAX_PATH];
            DWORD sz = MAX_PATH;
            if (h && QueryFullProcessImageNameW(h, 0, path, &sz) &&
                path_under_root(path, root))
                pids.push_back(pe.th32ProcessID);
            if (h) CloseHandle(h);
        }
        CloseHandle(snap);
    }
    if (pids.empty()) return;

    if (graceMs > 0) {
        printf("  requesting graceful stop (%zu process(es), wait up to %lu ms) ...\n",
               pids.size(), graceMs);

        /* 阶段1：AttachConsole 到每个子进程的控制台，发送 CTRL_BREAK 请求退出。
           发送前装一个忽略处理器，防止 Ctrl 事件把 mtc_ctl 自己一起杀掉。 */
        bool hadConsole = GetConsoleWindow() != nullptr;
        if (hadConsole) FreeConsole();
        SetConsoleCtrlHandler(nullptr, TRUE);          // 忽略 Ctrl+C / Ctrl+Break
        for (DWORD pid : pids) {
            if (AttachConsole(pid)) {
                GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
                FreeConsole();
            }
        }
        SetConsoleCtrlHandler(nullptr, FALSE);         // 恢复
        if (hadConsole) AttachConsole(ATTACH_PARENT_PROCESS);

        /* 阶段2：并发等待所有进程自行退出，整体等待不超过 graceMs */
        std::vector<HANDLE> waits;
        for (DWORD pid : pids) {
            HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (h) waits.push_back(h);
        }
        DWORD deadline = GetTickCount() + graceMs;
        for (size_t i = 0; i < waits.size() && !waits.empty(); i++) {
            DWORD now = GetTickCount();
            DWORD remain = now >= deadline ? 0 : deadline - now;
            if (remain == 0) break;
            WaitForSingleObject(waits[i], remain);
        }
        for (HANDLE h : waits) CloseHandle(h);
    }

    /* 阶段3：仍存活者强制结束 */
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                               FALSE, pid);
        if (!h) continue;
        DWORD code = 0;
        if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE) {
            printf("  force-killing PID %lu (did not exit gracefully)\n", pid);
            TerminateProcess(h, 1);
        }
        CloseHandle(h);
    }
}

/* 启动子进程：可选重定向 stdout/stderr 到日志文件；visible=false 时隐藏窗口 */
bool spawn_proc(const std::string &exe, const std::vector<std::string> &args,
                const std::string &cwd, const std::string &logFile,
                bool visible, DWORD *pidOut)
{
    std::wstring cmd = L"\"" + utf8_to_wide(exe) + L"\"";
    for (const auto &a : args) {
        std::wstring wa = utf8_to_wide(a);
        if (wa.find(L' ') != std::wstring::npos)
            cmd += L" \"" + wa + L"\"";
        else
            cmd += L" " + wa;
    }

    HANDLE hLog = INVALID_HANDLE_VALUE;
    if (!logFile.empty())
        hLog = CreateFileW(utf8_to_wide(logFile).c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (hLog != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
    }

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    DWORD flags = CREATE_NEW_PROCESS_GROUP;
    if (!visible) flags |= CREATE_NO_WINDOW;

    std::wstring cwdw = utf8_to_wide(cwd);
    BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr,
                             hLog != INVALID_HANDLE_VALUE ? TRUE : FALSE,
                             flags, nullptr,
                             cwdw.empty() ? nullptr : cwdw.c_str(), &si, &pi);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!ok) return false;
    if (pidOut) *pidOut = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

/* 前台运行并等待结束，返回退出码 */
int run_wait(const std::string &exe, const std::vector<std::string> &args,
             const std::string &cwd)
{
    std::wstring cmd = L"\"" + utf8_to_wide(exe) + L"\"";
    for (const auto &a : args) {
        std::wstring wa = utf8_to_wide(a);
        if (wa.find(L' ') != std::wstring::npos)
            cmd += L" \"" + wa + L"\"";
        else
            cmd += L" " + wa;
    }
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::wstring cwdw = utf8_to_wide(cwd);
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr,
                        cwdw.empty() ? nullptr : cwdw.c_str(), &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

/* ------------------------------------------------------------------ */
/* HTTP GET（WinHTTP）                                                 */
/* ------------------------------------------------------------------ */

bool http_get(int port, const std::string &path, std::string &body)
{
    body.clear();
    HINTERNET hSession = WinHttpOpen(L"mtc-ctl/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    bool ok = false;
    HINTERNET hConn = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)port, 0);
    if (hConn) {
        wchar_t wpath[1024];
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath, 1024);
        HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", wpath, nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hReq) {
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                char buf[8192];
                DWORD avail = 0, read = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    if (!WinHttpReadData(hReq, buf, min(avail, (DWORD)sizeof(buf)),
                                         &read))
                        break;
                    body.append(buf, read);
                    if (read < avail) break;
                }
                ok = true;
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConn);
    }
    WinHttpCloseHandle(hSession);
    return ok;
}

std::string between(const std::string &s, const std::string &open)
{
    size_t p = s.find(open);
    if (p == std::string::npos) return "";
    size_t gt = s.find('>', p);
    size_t lt = gt == std::string::npos ? std::string::npos : s.find('<', gt + 1);
    if (gt == std::string::npos || lt == std::string::npos) return "";
    return s.substr(gt + 1, lt - gt - 1);
}

/* 提取 s 中第一个 name="..." 属性值 */
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

/* ------------------------------------------------------------------ */
/* 选项解析                                                            */
/* ------------------------------------------------------------------ */

struct Options {
    int httpPort = 5000;
    int shdrBase = 7878;
    int webPort = 8088;
    int pollIntervalMs = 5000;
    int pruneDays = 90;
    int alertMin = 60;
    std::string root;
    std::string jichuang;
    std::string agentDir;
    std::string db = "stats.db";
    std::string webRoot = "web/dist";
    std::string alertUrl;
    std::string logDir;
    std::string devicesDir;
    bool monitorConfig = false;
    int bufferSize = 17;
    bool noPoll = false;
    bool noWeb = false;
    bool visible = false;
};

bool parse_opts(int argc, char *argv[], int start, Options &o,
                std::vector<std::string> &positional)
{
    for (int i = start; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            return i + 1 < argc ? argv[++i] : "";
        };
        if (a == "--http-port") o.httpPort = atoi(next().c_str());
        else if (a == "--shdr-base") o.shdrBase = atoi(next().c_str());
        else if (a == "--web-port") o.webPort = atoi(next().c_str());
        else if (a == "--agent-port") o.httpPort = atoi(next().c_str());
        else if (a == "--interval-ms") o.pollIntervalMs = atoi(next().c_str());
        else if (a == "--prune-days") o.pruneDays = atoi(next().c_str());
        else if (a == "--alert-min") o.alertMin = atoi(next().c_str());
        else if (a == "--alert-url") o.alertUrl = next();
        else if (a == "--root") o.root = next();
        else if (a == "--jichuang") o.jichuang = next();
        else if (a == "--agent-dir") o.agentDir = next();
        else if (a == "--db") o.db = next();
        else if (a == "--web-root") o.webRoot = next();
        else if (a == "--no-poll") o.noPoll = true;
        else if (a == "--no-web") o.noWeb = true;
        else if (a == "--visible") o.visible = true;
        else if (!a.empty() && a[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", a.c_str());
            return false;
        } else positional.push_back(a);
    }
    return true;
}

void resolve_paths(Options &o)
{
    if (o.root.empty()) o.root = default_root();
    char abs[MAX_PATH];
    if (GetFullPathNameA(o.root.c_str(), MAX_PATH, abs, nullptr))
        o.root = abs;
    if (o.agentDir.empty()) o.agentDir = join_path(o.root, "agent");
    else if (o.agentDir.find(':') == std::string::npos)
        o.agentDir = join_path(o.root, o.agentDir);
    if (o.devicesDir.empty()) o.devicesDir = join_path(o.root, "devices");
    else if (o.devicesDir.find(':') == std::string::npos)
        o.devicesDir = join_path(o.root, o.devicesDir);
    if (o.logDir.empty()) o.logDir = join_path(o.root, "log");
    else if (o.logDir.find(':') == std::string::npos)
        o.logDir = join_path(o.root, o.logDir);
    if (o.jichuang.empty()) {
        std::string local = join_path(o.root, "jichuang.txt");
        if (GetFileAttributesA(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
            o.jichuang = local;
        } else {
            size_t slash = o.root.find_last_of("\\/");
            std::string parent = slash == std::string::npos
                                     ? o.root
                                     : o.root.substr(0, slash);
            o.jichuang = join_path(parent, "jichuang.txt");
        }
    }
}

/* 读取 agent/adapters.txt：TYPE NAME IP PORT SHDRPORT */
struct AdapterLine {
    std::string type, name, ip, port, shdr;
};

std::vector<AdapterLine> read_adapters(const std::string &path)
{
    std::vector<AdapterLine> out;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto t = split_ws(line);
        if (t.size() < 5) continue;
        AdapterLine a;
        a.type = lower_str(t[0]);
        a.name = t[1];
        a.ip = t[2];
        a.port = t[3];
        a.shdr = t[4];
        out.push_back(a);
    }
    fclose(f);
    return out;
}

/* ------------------------------------------------------------------ */
/* 子命令                                                              */
/* ------------------------------------------------------------------ */

int cmd_start(const Options &raw, bool simAll)
{
    Options o = raw;
    resolve_paths(o);
    if (GetFileAttributesA(o.jichuang.c_str()) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[ERROR] jichuang.txt not found: %s\n", o.jichuang.c_str());
        return 1;
    }

    printf("[mtc_ctl] root: %s\n", o.root.c_str());
    printf("[mtc_ctl] machine list: %s\n", o.jichuang.c_str());

    printf("[1/5] Stopping leftover services ...\n");
    stop_services(utf8_to_wide(o.root));

    printf("[2/5] Generating agent configuration ...\n");
    {
        std::vector<std::string> args = {
            o.jichuang, o.agentDir, std::to_string(o.httpPort),
            std::to_string(o.shdrBase), "127.0.0.1", o.devicesDir,
            o.monitorConfig ? "1" : "0", std::to_string(o.bufferSize)
        };
        int rc = run_wait(join_path(o.root, "bin\\genconfig.exe"), args, o.root);
        if (rc != 0) {
            fprintf(stderr, "[ERROR] genconfig failed (%d)\n", rc);
            return 1;
        }
    }

    std::string logDir = o.logDir;
    CreateDirectoryA(logDir.c_str(), nullptr);

    std::string binDir = join_path(o.root, "bin");
    std::string adapters = read_adapters(join_path(o.agentDir, "adapters.txt")).empty()
                               ? join_path(o.root, "adapters.txt")
                               : join_path(o.agentDir, "adapters.txt");
    auto lines = read_adapters(adapters);
    if (lines.empty()) {
        fprintf(stderr, "[ERROR] no adapters in %s\n", adapters.c_str());
        return 1;
    }

    printf("[3/5] Starting collectors (%zu machines) ...\n", lines.size());
    for (const auto &a : lines) {
        if (simAll || a.type == "sim") {
            int shdr = atoi(a.shdr.c_str());
            int ctl = shdr + 2000;
            printf("  SIM    %s -> SHDR %s (control :%d)\n",
                   a.name.c_str(), a.shdr.c_str(), ctl);
            spawn_proc(join_path(binDir, "cnc_sim.exe"),
                       { a.shdr, std::to_string(ctl), "500", a.name },
                       o.root, join_path(logDir, a.name + ".log"), o.visible, nullptr);
        } else if (a.type == "fanuc") {
            std::string adir = join_path(join_path(o.agentDir, "adapters"), a.name);
            CreateDirectoryA(adir.c_str(), nullptr);
            printf("  FANUC  %s %s:%s -> SHDR %s\n",
                   a.name.c_str(), a.ip.c_str(), a.port.c_str(), a.shdr.c_str());
            spawn_proc(join_path(binDir, "fanuc_adapter.exe"),
                       { "run", a.ip, a.port, a.shdr },
                       adir, join_path(logDir, a.name + ".log"), o.visible, nullptr);
        } else if (a.type == "mazak") {
            printf("  MAZAK  %s %s:%s -> SHDR %s\n",
                   a.name.c_str(), a.ip.c_str(), a.port.c_str(), a.shdr.c_str());
            spawn_proc(join_path(binDir, "mazak_adapter.exe"),
                       { "run", a.ip, a.port, a.shdr },
                       o.root, join_path(logDir, a.name + ".log"), o.visible, nullptr);
        } else if (a.type == "shdr") {
            printf("  SHDR   %s passthrough %s:%s\n",
                   a.name.c_str(), a.ip.c_str(), a.port.c_str());
        } else {
            printf("  [warn] unknown type '%s' for %s, skipped\n",
                   a.type.c_str(), a.name.c_str());
        }
    }

    printf("[4/5] Starting MTConnect agent (http://127.0.0.1:%d) ...\n", o.httpPort);
    spawn_proc(join_path(o.agentDir, "agent.exe"),
               { "run", "agent.cfg" }, o.agentDir,
               join_path(logDir, "agent_console.log"), o.visible, nullptr);

    if (!o.noPoll) {
        printf("  starting stats poller (mtc_stats stream) ...\n");
        std::vector<std::string> args = {
            "stream", std::to_string(o.httpPort), o.db,
            std::to_string(o.pollIntervalMs), std::to_string(o.pruneDays),
            o.alertUrl.empty() ? "-" : o.alertUrl, std::to_string(o.alertMin)
        };
        spawn_proc(join_path(binDir, "mtc_stats.exe"), args, o.root,
                   join_path(logDir, "stats_poll.log"), o.visible, nullptr);
    }
    if (!o.noWeb) {
        printf("  starting web server (http://127.0.0.1:%d) ...\n", o.webPort);
        spawn_proc(join_path(binDir, "webserver.exe"),
                   { std::to_string(o.webPort), o.db, std::to_string(o.httpPort),
                     o.webRoot },
                   o.root, join_path(logDir, "webserver.log"), o.visible, nullptr);
    }

    printf("[5/5] Done.\n");
    printf("  agent : http://127.0.0.1:%d/{probe,current,sample}\n", o.httpPort);
    printf("  web   : http://127.0.0.1:%d\n", o.webPort);
    printf("  logs  : %s\\\n", logDir.c_str());
    printf("  stop  : mtc_ctl stop\n");
    return 0;
}

int cmd_stop(const Options &raw)
{
    Options o = raw;
    resolve_paths(o);
    printf("[mtc_ctl] stopping services under %s ...\n", o.root.c_str());
    stop_services(utf8_to_wide(o.root));
    printf("[mtc_ctl] done.\n");
    return 0;
}

int cmd_status(const Options &raw)
{
    Options o = raw;
    resolve_paths(o);

    printf("=== 服务进程 (root: %s) ===\n", o.root.c_str());
    auto running = running_services(utf8_to_wide(o.root));
    static const wchar_t *kOrder[] = {
        L"agent.exe", L"fanuc_adapter.exe", L"mazak_adapter.exe",
        L"cnc_sim.exe", L"shdr_sim.exe", L"mazak_sim.exe",
        L"mtc_stats.exe", L"webserver.exe"
    };
    for (auto *name : kOrder) {
        auto it = running.find(name);
        if (it != running.end() && !it->second.empty()) {
            printf("  %-18ls running (PID", name);
            for (size_t i = 0; i < it->second.size(); i++)
                printf(i ? ", %lu" : " %lu", it->second[i]);
            printf(")\n");
        } else {
            printf("  %-18ls stopped\n", name);
        }
    }

    printf("\n=== 机床连接 (http://127.0.0.1:%d/current) ===\n", o.httpPort);
    std::string xml;
    int rc = 1;
    if (http_get(o.httpPort, "/current", xml) && !xml.empty()) {
        rc = 0;
        printf("  %-10s %-14s %-12s %s\n", "Machine", "Availability", "Execution", "Mode");
        printf("  %s\n", std::string(46, '-').c_str());
        size_t pos = 0;
        while ((pos = xml.find("<DeviceStream ", pos)) != std::string::npos) {
            size_t close = xml.find("</DeviceStream>", pos);
            if (close == std::string::npos) break;
            std::string block = xml.substr(pos, close - pos + 15);
            std::string name = attr_of(block, "name");
            if (name.empty() || name == "Agent") {
                pos = close + 15;
                continue;
            }
            std::string avail = between(block, "<Availability");
            std::string exec = between(block, "<Execution");
            std::string mode = between(block, "<ControllerMode");
            if (avail.empty()) avail = "-";
            if (exec.empty()) exec = "-";
            if (mode.empty()) mode = "-";
            printf("  %-10s %-14s %-12s %s\n",
                   name.c_str(), avail.c_str(), exec.c_str(), mode.c_str());
            pos = close + 15;
        }
    } else {
        printf("  [ERROR] agent not reachable on port %d. Start it with: mtc_ctl start\n",
               o.httpPort);
    }

    printf("\n=== Web 健康 (http://127.0.0.1:%d/api/health) ===\n", o.webPort);
    std::string health;
    if (http_get(o.webPort, "/api/health", health) && !health.empty()) {
        printf("  %s\n", health.c_str());
    } else {
        printf("  [INFO] webserver not reachable (poll/web not started?)\n");
    }
    return rc;
}

int cmd_poll(const Options &raw)
{
    Options o = raw;
    resolve_paths(o);
    std::string logDir = o.logDir;
    CreateDirectoryA(logDir.c_str(), nullptr);
    std::vector<std::string> args = {
        "stream", std::to_string(o.httpPort), o.db,
        std::to_string(o.pollIntervalMs), std::to_string(o.pruneDays),
        o.alertUrl.empty() ? "-" : o.alertUrl, std::to_string(o.alertMin)
    };
    DWORD pid = 0;
    if (!spawn_proc(join_path(o.root, "bin\\mtc_stats.exe"), args, o.root,
                    join_path(logDir, "stats_poll.log"), o.visible, &pid)) {
        fprintf(stderr, "[ERROR] failed to start mtc_stats.exe\n");
        return 1;
    }
    printf("[mtc_ctl] stats poller started (PID %lu, http://127.0.0.1:%d, db=%s)\n",
           pid, o.httpPort, o.db.c_str());
    return 0;
}

int cmd_web(const Options &raw)
{
    Options o = raw;
    resolve_paths(o);
    std::string logDir = o.logDir;
    CreateDirectoryA(logDir.c_str(), nullptr);
    DWORD pid = 0;
    if (!spawn_proc(join_path(o.root, "bin\\webserver.exe"),
                    { std::to_string(o.webPort), o.db, std::to_string(o.httpPort),
                      o.webRoot },
                    o.root, join_path(logDir, "webserver.log"), o.visible, &pid)) {
        fprintf(stderr, "[ERROR] failed to start webserver.exe\n");
        return 1;
    }
    printf("[mtc_ctl] webserver started (PID %lu, http://127.0.0.1:%d, db=%s)\n",
           pid, o.webPort, o.db.c_str());
    return 0;
}

int cmd_report(const Options &raw, const std::vector<std::string> &positional)
{
    Options o = raw;
    resolve_paths(o);
    std::vector<std::string> args = { "report", o.db };
    for (size_t i = 0; i < positional.size() && i < 3; i++)
        args.push_back(positional[i]);
    int rc = run_wait(join_path(o.root, "bin\\mtc_stats.exe"), args, o.root);
    if (rc < 0) {
        fprintf(stderr, "[ERROR] failed to run mtc_stats.exe\n");
        return 1;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Windows 服务（install / uninstall / service）                        */
/*   服务二进制即 mtc_ctl.exe 本身：SCM 以 `mtc_ctl.exe service [options]` */
/*   启动。`service` 是专用命令字，与交互式 CLI 命令（start/stop/...）互不 */
/*   冲突，从而化解“服务事件传参”与“本程序已有命令行参数”的冲突。          */
/* ------------------------------------------------------------------ */

static const wchar_t kMtcSvcName[] = L"mtc_ctl";
static const wchar_t kMtcSvcDisplay[] = L"MTConnect 数据采集服务";

static SERVICE_STATUS          g_svcStatus;
static SERVICE_STATUS_HANDLE   g_svcHandle = nullptr;
static HANDLE                  g_stopEvent = nullptr;
static Options                 g_svcOptions;

static VOID WINAPI SvcMain(DWORD dwArgc, LPWSTR *lpszArgv);
static VOID WINAPI SvcCtrlHandler(DWORD ctrl);
static void ReportSvcStatus(DWORD state, DWORD exitCode, DWORD waitHint);

static VOID WINAPI SvcMain(DWORD, LPWSTR *)
{
    /* 服务进程没有控制台，把 stdout/stderr 重定向到 log\service.log */
    if (!g_svcOptions.logDir.empty()) {
        CreateDirectoryA(g_svcOptions.logDir.c_str(), nullptr);
        std::string slog = join_path(g_svcOptions.logDir, "service.log");
        freopen(slog.c_str(), "a", stdout);
        freopen(slog.c_str(), "a", stderr);
    }

    g_svcHandle = RegisterServiceCtrlHandlerW(kMtcSvcName, SvcCtrlHandler);
    if (!g_svcHandle) {
        fprintf(stderr, "RegisterServiceCtrlHandler failed (%lu)\n", GetLastError());
        return;
    }
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_svcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svcStatus.dwServiceSpecificExitCode = 0;

    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    printf("[mtc_ctl] service starting (root: %s)\n", g_svcOptions.root.c_str());
    int rc = cmd_start(g_svcOptions, false);
    if (rc != 0) {
        fprintf(stderr, "[mtc_ctl] service pipeline failed to start (rc=%d)\n", rc);
        ReportSvcStatus(SERVICE_STOPPED, (DWORD)rc, 0);
        return;
    }
    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
    printf("[mtc_ctl] service running\n");

    /* 一直运行，直到收到 STOP / SHUTDOWN 控制码 */
    WaitForSingleObject(g_stopEvent, INFINITE);
    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
    printf("[mtc_ctl] service stopped\n");
}

static VOID WINAPI SvcCtrlHandler(DWORD ctrl)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 8000);
        printf("[mtc_ctl] stopping service ...\n");
        /* 结束由本工具启动的 agent/采集器/统计/Web 子进程 */
        if (!g_svcOptions.root.empty())
            stop_services(utf8_to_wide(g_svcOptions.root));
        if (g_stopEvent) SetEvent(g_stopEvent);
        break;
    default:
        break;
    }
}

static void ReportSvcStatus(DWORD state, DWORD exitCode, DWORD waitHint)
{
    static DWORD checkPoint = 1;
    g_svcStatus.dwCurrentState = state;
    g_svcStatus.dwWin32ExitCode = exitCode;
    g_svcStatus.dwWaitHint = waitHint;
    if (state == SERVICE_START_PENDING)
        g_svcStatus.dwControlsAccepted = 0;
    else
        g_svcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
        g_svcStatus.dwCheckPoint = 0;
    else
        g_svcStatus.dwCheckPoint = checkPoint++;
    if (g_svcHandle)
        SetServiceStatus(g_svcHandle, &g_svcStatus);
}

/* 注册服务控制调度器并阻塞运行；若进程非 SCM 启动则失败并给出提示 */
int cmd_service(const Options &raw)
{
    Options o = raw;
    resolve_paths(o);
    g_svcOptions = o;
    SERVICE_TABLE_ENTRYW table[] = {
        { (LPWSTR)kMtcSvcName, SvcMain },
        { nullptr, nullptr }
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            fprintf(stderr,
                    "[ERROR] 'service' 命令只能由服务控制管理器(SCM)启动，不能直接在命令行运行。\n"
                    "        请先安装服务: mtc_ctl install\n");
            return 1;
        }
        fprintf(stderr, "[ERROR] StartServiceCtrlDispatcher failed (%lu)\n", err);
        return 1;
    }
    return 0;
}

/* 安装/更新 Windows 服务：ImagePath 指向 mtc_ctl.exe 本身 + service 命令字 */
int cmd_install(const Options &raw)
{
    Options o = raw;
    resolve_paths(o);
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
        fprintf(stderr, "[ERROR] cannot get module path (%lu)\n", GetLastError());
        return 1;
    }
    std::string image = "\"" + std::string(exe) + "\" service --root \"" + o.root + "\""
                        + " --http-port " + std::to_string(o.httpPort)
                        + " --shdr-base " + std::to_string(o.shdrBase)
                        + " --web-port " + std::to_string(o.webPort);

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        fprintf(stderr, "[ERROR] OpenSCManager failed (%lu). 需要管理员权限。\n", GetLastError());
        return 1;
    }
    SC_HANDLE svc = OpenServiceW(scm, kMtcSvcName, SERVICE_ALL_ACCESS);
    if (svc) {
        if (!ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_AUTO_START,
                                  SERVICE_NO_CHANGE, utf8_to_wide(image).c_str(),
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)) {
            fprintf(stderr, "[ERROR] ChangeServiceConfig failed (%lu)\n", GetLastError());
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return 1;
        }
        printf("[mtc_ctl] service '%ls' updated\n", kMtcSvcName);
    } else {
        svc = CreateServiceW(scm, kMtcSvcName, kMtcSvcDisplay,
                             SERVICE_ALL_ACCESS,
                             SERVICE_WIN32_OWN_PROCESS,
                             SERVICE_AUTO_START,
                             SERVICE_ERROR_NORMAL,
                             utf8_to_wide(image).c_str(),
                             nullptr, nullptr, nullptr, nullptr, nullptr);
        if (!svc) {
            fprintf(stderr, "[ERROR] CreateService failed (%lu). 需要管理员权限。\n", GetLastError());
            CloseServiceHandle(scm);
            return 1;
        }
        printf("[mtc_ctl] service '%ls' installed (auto start)\n", kMtcSvcName);
    }
    printf("[mtc_ctl] image: %s\n", image.c_str());
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

/* 删除 Windows 服务 */
int cmd_uninstall(const Options &raw)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        fprintf(stderr, "[ERROR] OpenSCManager failed (%lu). 需要管理员权限。\n", GetLastError());
        return 1;
    }
    SC_HANDLE svc = OpenServiceW(scm, kMtcSvcName, DELETE);
    if (!svc) {
        fprintf(stderr, "[ERROR] service '%ls' not installed (%lu)\n", kMtcSvcName, GetLastError());
        CloseServiceHandle(scm);
        return 1;
    }
    if (!DeleteService(svc)) {
        fprintf(stderr, "[ERROR] DeleteService failed (%lu)\n", GetLastError());
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return 1;
    }
    printf("[mtc_ctl] service '%ls' deleted\n", kMtcSvcName);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

void usage(const char *prog)
{
    printf("MTConnect 采集服务控制台\n\n");
    printf("  %s start [http_port] [shdr_base] [options]\n", prog);
    printf("      启动全部：genconfig -> 采集器 -> agent -> poll -> web\n");
    printf("  %s test [http_port] [shdr_base] [options]\n", prog);
    printf("      全 SIM 演示：所有机台启动 cnc_sim，6s 后打印状态\n");
    printf("  %s stop [options]\n", prog);
    printf("      停止 root 下的服务进程\n");
    printf("  %s status [http_port] [options]\n", prog);
    printf("      进程状态 + 机床连接表 + web 健康检查\n");
    printf("  %s poll [options]\n", prog);
    printf("      后台启动统计采集（mtc_stats stream）\n");
    printf("  %s web [options]\n", prog);
    printf("      后台启动 Web 服务（webserver）\n");
    printf("  %s report [bucket] [from] [to] [options]\n", prog);
    printf("      运行统计报表（默认 stats.db，1800s，最近24h）\n");
    printf("  %s install [options]\n", prog);
    printf("      安装/更新 Windows 服务（二进制即本程序，开机自启）\n");
    printf("  %s uninstall\n", prog);
    printf("      删除已安装的 Windows 服务\n\n");
    printf("options:\n");
    printf("  --http-port N   agent HTTP 端口 (默认 5000)\n");
    printf("  --shdr-base N   SHDR 起始端口 (默认 7878)\n");
    printf("  --web-port N    web 端口 (默认 8088)\n");
    printf("  --root DIR      项目根目录 (默认: exe 上一级)\n");
    printf("  --jichuang PATH 机器清单路径\n");
    printf("  --agent-dir DIR agent 目录 (默认 <root>\\agent)\n");
    printf("  --db PATH       统计数据库 (默认 stats.db)\n");
    printf("  --interval-ms N poll 采样间隔毫秒 (默认 5000)\n");
    printf("  --prune-days N  保留天数 (默认 90)\n");
    printf("  --alert-url URL webhook 通知地址 (stream 第6参数)\n");
    printf("  --alert-min N   重复告警间隔分钟 (默认 60)\n");
    printf("  --no-poll / --no-web   启动时跳过 poll/web\n");
    printf("  --visible       采集器/服务显示窗口（默认隐藏）\n");
}

} // namespace

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    std::string cmd = lower_str(argv[1]);
    Options o;
    /* config.json 提供默认值（./config.json -> %USERPROFILE%\mtconnect\config.json），
       命令行参数仍可覆盖 */
    cfg::Config c;
    std::string cerr;
    cfg::load(c, "", &cerr);
    if (!cerr.empty()) fprintf(stderr, "[mtc_ctl] %s\n", cerr.c_str());
    o.httpPort = c.agent_http_port;
    o.shdrBase = c.shdr_base_port;
    o.webPort = c.web_port;
    o.pollIntervalMs = c.stream_interval_ms;
    o.pruneDays = c.retention_days;
    o.alertMin = c.alert_min;
    o.alertUrl = c.alert_url;
    o.db = c.db_path;
    o.webRoot = c.web_root;
    o.agentDir = c.agent_dir;
    o.logDir = c.log_dir;
    o.devicesDir = c.devices_dir;
    o.monitorConfig = c.monitor_config_files;
    o.bufferSize = c.agent_buffer_size;
    std::vector<std::string> positional;
    if (!parse_opts(argc, argv, 2, o, positional)) return 1;

    /* start/test 的前两个位置参数为 http_port / shdr_base */
    if (cmd == "start" || cmd == "test") {
        if (positional.size() > 0) o.httpPort = atoi(positional[0].c_str());
        if (positional.size() > 1) o.shdrBase = atoi(positional[1].c_str());
    }
    if (cmd == "status" && !positional.empty())
        o.httpPort = atoi(positional[0].c_str());
    if (cmd == "poll" && !positional.empty())
        o.httpPort = atoi(positional[0].c_str());
    if (cmd == "web" && !positional.empty())
        o.webPort = atoi(positional[0].c_str());

    if (cmd == "start") return cmd_start(o, false);
    if (cmd == "test") return cmd_start(o, true);
    if (cmd == "stop") return cmd_stop(o);
    if (cmd == "status") return cmd_status(o);
    if (cmd == "poll") return cmd_poll(o);
    if (cmd == "web") return cmd_web(o);
    if (cmd == "report") return cmd_report(o, positional);
    if (cmd == "install") return cmd_install(o);
    if (cmd == "uninstall" || cmd == "delete") return cmd_uninstall(o);
    if (cmd == "service") return cmd_service(o);

    fprintf(stderr, "unknown command '%s'\n", argv[1]);
    usage(argv[0]);
    return 1;
}
