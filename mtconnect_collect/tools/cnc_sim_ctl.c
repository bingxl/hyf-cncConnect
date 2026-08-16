/*
 * cnc_sim_ctl.c - command line control tool for cnc_sim.exe
 *
 * Talks to the simulator's local HTTP control API:
 *   state / help                 GET
 *   start|stop|hold|resume|...   POST /control with JSON
 *
 * Usage:
 *   cnc_sim_ctl.exe <control_port> <command> [args...]
 *
 * Examples:
 *   cnc_sim_ctl.exe 9878 state
 *   cnc_sim_ctl.exe 9878 start
 *   cnc_sim_ctl.exe 9878 mode MANUAL
 *   cnc_sim_ctl.exe 9878 program O2000
 *   cnc_sim_ctl.exe 9878 alarm spindle
 *   cnc_sim_ctl.exe 9878 jog X + 20
 *   cnc_sim_ctl.exe 9878 set Fovr 80
 *   cnc_sim_ctl.exe 9878 auto on
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int http_request(int port, LPCWSTR method, const char *path, const char *body)
{
    HINTERNET hs = WinHttpOpen(L"cnc-sim-ctl/1.0",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) {
        fprintf(stderr, "WinHttpOpen failed (%lu)\n", GetLastError());
        return 1;
    }

    HINTERNET hc = WinHttpConnect(hs, L"127.0.0.1", (INTERNET_PORT)port, 0);
    if (!hc) {
        fprintf(stderr, "WinHttpConnect failed (%lu) - is cnc_sim running?\n",
                GetLastError());
        WinHttpCloseHandle(hs);
        return 1;
    }

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    HINTERNET hr = WinHttpOpenRequest(hc, method, wpath, NULL,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hr) {
        fprintf(stderr, "WinHttpOpenRequest failed (%lu)\n", GetLastError());
        WinHttpCloseHandle(hc);
        WinHttpCloseHandle(hs);
        return 1;
    }

    DWORD blen = body ? (DWORD)strlen(body) : 0;
    BOOL ok = WinHttpSendRequest(hr,
                                 L"Content-Type: application/json\r\n", -1L,
                                 (LPVOID)(body ? body : ""), blen, blen, 0);
    if (ok) ok = WinHttpReceiveResponse(hr, NULL);
    if (!ok) {
        fprintf(stderr, "request failed (%lu)\n", GetLastError());
        WinHttpCloseHandle(hr);
        WinHttpCloseHandle(hc);
        WinHttpCloseHandle(hs);
        return 1;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE |
                            WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                        WINHTTP_NO_HEADER_INDEX);

    char out[65536];
    DWORD total = 0;
    DWORD rd = 0;
    while (total < sizeof(out) - 1) {
        if (!WinHttpReadData(hr, out + total, (DWORD)(sizeof(out) - 1 - total),
                             &rd) || rd == 0)
            break;
        total += rd;
    }
    out[total] = '\0';
    printf("HTTP %lu\n%s\n", status, out);

    WinHttpCloseHandle(hr);
    WinHttpCloseHandle(hc);
    WinHttpCloseHandle(hs);
    return 0;
}

static void usage(void)
{
    printf("Usage: cnc_sim_ctl.exe <control_port> <command> [args...]\n");
    printf("\n");
    printf("  state | help\n");
    printf("  start | stop | hold | resume | reset | estop | estop_release\n");
    printf("  mode <AUTOMATIC|MANUAL|MDI>\n");
    printf("  program <O1000|O2000|O3000>\n");
    printf("  alarm <none|spindle|servo|overtravel|overheat|comms|logic|motion|system>\n");
    printf("  jog <axis> <dir> <dist>       e.g. jog X + 20\n");
    printf("  mdi <axis> <dist>             e.g. mdi X 20\n");
    printf("  set <key> <value>             Fovr | SspeedOvr | part_required |\n");
    printf("                                part_total | part_current | spindle\n");
    printf("  setpos <axis> <value>         e.g. setpos X 12.5\n");
    printf("  auto <on|off>                 re-enable / disable auto cycle\n");
}

static int need(int argc, char *argv[], int idx, const char *what)
{
    if (idx >= argc) {
        fprintf(stderr, "missing argument: %s\n", what);
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        usage();
        return 1;
    }

    int port = atoi(argv[1]);
    const char *cmd = argv[2];
    char body[512];

    if (strcmp(cmd, "state") == 0 || strcmp(cmd, "status") == 0)
        return http_request(port, L"GET", "/state", NULL);
    if (strcmp(cmd, "help") == 0)
        return http_request(port, L"GET", "/", NULL);

    if (strcmp(cmd, "start") == 0 || strcmp(cmd, "stop") == 0 ||
        strcmp(cmd, "hold") == 0 || strcmp(cmd, "resume") == 0 ||
        strcmp(cmd, "reset") == 0 || strcmp(cmd, "estop") == 0 ||
        strcmp(cmd, "estop_release") == 0) {
        _snprintf(body, sizeof(body), "{\"cmd\":\"%s\"}", cmd);
    } else if (strcmp(cmd, "mode") == 0) {
        if (!need(argc, argv, 3, "mode value")) return 1;
        _snprintf(body, sizeof(body), "{\"cmd\":\"mode\",\"mode\":\"%s\"}", argv[3]);
    } else if (strcmp(cmd, "program") == 0) {
        if (!need(argc, argv, 3, "program number")) return 1;
        _snprintf(body, sizeof(body), "{\"cmd\":\"program\",\"program\":\"%s\"}", argv[3]);
    } else if (strcmp(cmd, "alarm") == 0) {
        if (!need(argc, argv, 3, "alarm name")) return 1;
        _snprintf(body, sizeof(body), "{\"cmd\":\"alarm\",\"alarm\":\"%s\"}", argv[3]);
    } else if (strcmp(cmd, "jog") == 0) {
        if (!need(argc, argv, 3, "axis") ||
            !need(argc, argv, 4, "direction (+/-)") ||
            !need(argc, argv, 5, "distance")) return 1;
        _snprintf(body, sizeof(body),
                  "{\"cmd\":\"jog\",\"axis\":\"%s\",\"dir\":\"%s\",\"dist\":\"%s\"}",
                  argv[3], argv[4], argv[5]);
    } else if (strcmp(cmd, "mdi") == 0) {
        if (!need(argc, argv, 3, "axis") || !need(argc, argv, 4, "distance")) return 1;
        _snprintf(body, sizeof(body),
                  "{\"cmd\":\"mdi\",\"axis\":\"%s\",\"dist\":\"%s\"}",
                  argv[3], argv[4]);
    } else if (strcmp(cmd, "set") == 0) {
        if (!need(argc, argv, 3, "key") || !need(argc, argv, 4, "value")) return 1;
        _snprintf(body, sizeof(body),
                  "{\"cmd\":\"set\",\"key\":\"%s\",\"value\":\"%s\"}",
                  argv[3], argv[4]);
    } else if (strcmp(cmd, "setpos") == 0) {
        if (!need(argc, argv, 3, "axis") || !need(argc, argv, 4, "value")) return 1;
        _snprintf(body, sizeof(body),
                  "{\"cmd\":\"setpos\",\"axis\":\"%s\",\"value\":\"%s\"}",
                  argv[3], argv[4]);
    } else if (strcmp(cmd, "auto") == 0) {
        if (!need(argc, argv, 3, "on|off")) return 1;
        _snprintf(body, sizeof(body), "{\"cmd\":\"auto\",\"auto\":\"%s\"}", argv[3]);
    } else {
        fprintf(stderr, "unknown command: %s\n", cmd);
        usage();
        return 1;
    }

    return http_request(port, L"POST", "/control", body);
}
