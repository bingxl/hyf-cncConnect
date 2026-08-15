/*
 * shdr_sim.c - Minimal MTConnect SHDR simulator.
 *
 * Emits fake FANUC-style data (positions, spindle, status, macros) every
 * <interval> ms so the full chain (adapter port -> agent -> HTTP) can be
 * tested without a real CNC.
 *
 * Usage: shdr_sim.exe [port] [interval_ms]
 *   port      SHDR listen port (default 7878)
 *   interval  sample interval in ms (default 1000)
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CLIENTS 16

static SOCKET g_clients[MAX_CLIENTS];

static void utc_timestamp(char *buf, int len)
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    _snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

static void send_line(const char *ts, const char *key, const char *value)
{
    char line[512];
    _snprintf(line, sizeof(line), "%s|%s|%s\n", ts, key, value);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] != INVALID_SOCKET) {
            if (send(g_clients[i], line, (int)strlen(line), 0) == SOCKET_ERROR) {
                closesocket(g_clients[i]);
                g_clients[i] = INVALID_SOCKET;
            }
        }
    }
}
int main(int argc, char *argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : 7878;
    int interval = argc > 2 ? atoi(argv[2]) : 1000;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[shdr_sim] bind failed on port %d\n", port);
        return 1;
    }
    listen(srv, 8);
    for (int i = 0; i < MAX_CLIENTS; i++) g_clients[i] = INVALID_SOCKET;
    printf("[shdr_sim] listening on 127.0.0.1:%d, interval %dms\n", port, interval);

    /* non-blocking accept loop */
    u_long mode = 1;
    ioctlsocket(srv, FIONBIO, &mode);

    double x = 0.0;
    int part = 0;
    int running = 0;
    int tick = 0;
    while (1) {
        SOCKET c = accept(srv, NULL, NULL);
        if (c != INVALID_SOCKET) {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (g_clients[i] == INVALID_SOCKET) { g_clients[i] = c; break; }
            }
            printf("[shdr_sim] client connected\n");
        }
        Sleep(50);
        if (interval <= 0) continue;

        /* simulate a machine cycle every ~5 intervals */
        tick++;
        if (tick % 5 == 0) running = !running;

        char ts[64];
        utc_timestamp(ts, sizeof(ts));

        send_line(ts, "avail", "AVAILABLE");
        send_line(ts, "estop", "ARMED");
        send_line(ts, "execution", running ? "EXECUTING" : "STOPPED");
        send_line(ts, "mode", "AUTOMATIC");
        send_line(ts, "line", "100");
        send_line(ts, "program", "O0001");
        send_line(ts, "programInfo", "PART-ABC-001");
        send_line(ts, "block", "G01 X100.0 F100");
        if (running) x += 0.5;
        if (x > 120.0) { x = 0.0; part++; }

        send_line(ts, "Xact", "123.456");
        send_line(ts, "Xcom", "124.000");
        send_line(ts, "Xload", "35.5");
        send_line(ts, "Yact", "0.000");
        send_line(ts, "Ycom", "0.000");
        send_line(ts, "Yload", "2.1");
        send_line(ts, "Zact", "10.500");
        send_line(ts, "Zcom", "10.000");
        send_line(ts, "Zload", "40.2");

        send_line(ts, "pathFeedrate", "100.0");
        send_line(ts, "pathPosition", "123.456 0.000 10.500");
        send_line(ts, "Sspeed", "1500.0");
        send_line(ts, "Sload", "60.0");

        char buf[64];
        _snprintf(buf, sizeof(buf), "%d", part);
        send_line(ts, "part", buf);
        send_line(ts, "probe", "100.000 50.000 5.000");
        send_line(ts, "SspeedOvr", "100");
        send_line(ts, "Fovr", "120");

        /* conditions: normally NORMAL */
        send_line(ts, "servo", "NORMAL|||");
        send_line(ts, "spindle", "NORMAL|||");
        send_line(ts, "Xtravel", "NORMAL|||");

        Sleep(interval);
    }

    closesocket(srv);
    WSACleanup();
    return 0;
}
