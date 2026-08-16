/*
 * mazak_sim.c - Simulate a MAZAK MTConnect "pull-mode" endpoint.
 *
 * Listens on a TCP port; when the client sends:
 *   "MTConnect Streams\n"  -> responds with "tag|value" lines
 *   "MTConnect Probe\n"    -> responds with a device summary
 *   "MTConnect Changes\n"  -> responds with changed values
 *
 * Usage: mazak_sim.exe [port] [interval_ms]
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CLIENTS 16

static SOCKET g_clients[MAX_CLIENTS];
static int g_running = 1;

static void send_all(const char *buf)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] != INVALID_SOCKET) {
            if (send(g_clients[i], buf, (int)strlen(buf), 0) == SOCKET_ERROR) {
                closesocket(g_clients[i]);
                g_clients[i] = INVALID_SOCKET;
            }
        }
    }
}

/* build a "tag|value" data block with current fake state */
static void build_stream(char *out, int cap, int changes_only)
{
    int n = 0;
    static int part = 5000;
    static int exec_state = 0;
    static int tick = 0;

    tick++;
    if (tick % 5 == 0) { exec_state = !exec_state; part++; }

#define EMIT(fmt, ...) \
    do { int _w = _snprintf(out + n, cap - n - 2, fmt "\n", __VA_ARGS__); \
         if (_w > 0) n += _w; } while (0)

    if (!changes_only || tick % 3 == 0)
        EMIT("avail|AVAILABLE");
    EMIT("execution|%s", exec_state ? "ACTIVE" : "STOPPED");
    if (!changes_only || tick % 3 == 0)
        EMIT("mode|AUTOMATIC");
    if (!changes_only || tick % 3 == 0) {
        EMIT("program|O%04d", 1000 + tick % 5);
        EMIT("programInfo|PART-MZK-%03d", 100 + tick % 5);
    }
    EMIT("Xact|%.3f", 10.0 + (tick % 100) * 0.5);
    EMIT("Yact|%.3f", 5.0 + (tick % 50) * 0.2);
    EMIT("Zact|%.3f", 50.0 - (tick % 20) * 0.1);
    EMIT("Sspeed|%.1f", 1500.0 + (tick % 10) * 50.0);
    if (!changes_only || tick % 3 == 0)
        EMIT("pathFeedrate|%.1f", 120.0 + (tick % 20));
    EMIT("part_total|%d", part);
#undef EMIT
}

static DWORD WINAPI reader(LPVOID arg)
{
    SOCKET c = (SOCKET)(size_t)arg;
    char inbuf[4096];
    char pend[8192];
    size_t pend_len = 0;
    int n;

    while ((n = recv(c, inbuf, sizeof(inbuf) - 1, 0)) > 0) {
        /* accumulate into pending buffer to handle TCP fragmentation */
        if (pend_len + (size_t)n < sizeof(pend) - 1) {
            memcpy(pend + pend_len, inbuf, (size_t)n);
            pend_len += (size_t)n;
        }
        pend[pend_len] = '\0';

        /* process complete command lines */
        char *start = pend;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            char *cmd = start;
            char out[8192] = "";
            if (strstr(cmd, "Streams") || strstr(cmd, "Changes") || strstr(cmd, "Probe")) {
                if (strstr(cmd, "Probe"))
                    _snprintf(out, sizeof(out),
                              "Device: Mazak SIM\navail|AVAILABLE\nexecution|STOPPED\nmode|AUTOMATIC\n");
                else
                    build_stream(out, sizeof(out), strstr(cmd, "Changes") != NULL);
                if (send(c, out, (int)strlen(out), 0) == SOCKET_ERROR)
                    break;
            }
            start = nl + 1;
        }
        /* keep unprocessed remainder */
        pend_len = (size_t)(pend + pend_len - start);
        memmove(pend, start, pend_len);
    }
    closesocket(c);
    return 0;
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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[mazak_sim] bind failed on port %d\n", port);
        return 1;
    }
    listen(srv, 8);
    for (int i = 0; i < MAX_CLIENTS; i++) g_clients[i] = INVALID_SOCKET;
    printf("[mazak_sim] listening on port %d\n", port);

    u_long mode = 1;
    ioctlsocket(srv, FIONBIO, &mode);

    while (g_running) {
        SOCKET c = accept(srv, NULL, NULL);
        if (c != INVALID_SOCKET) {
            printf("[mazak_sim] client connected\n");
            /* handle commands in a dedicated thread per client */
            CreateThread(NULL, 0, reader, (LPVOID)(size_t)c, 0, NULL);
        }
        Sleep(50);
    }

    closesocket(srv);
    WSACleanup();
    return 0;
}
