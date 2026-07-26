#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "mazak_ops.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

static int g_wsa_initialized = 0;

static int ensure_wsa(void)
{
    WSADATA wsa;
    if (!g_wsa_initialized) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            printf("[ERROR] WSAStartup failed\n");
            return -1;
        }
        g_wsa_initialized = 1;
    }
    return 0;
}

static SOCKET create_socket(void)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        printf("[ERROR] socket() failed: %d\n", WSAGetLastError());
    return s;
}

static int socket_connect(SOCKET s, const char *ip, int port, int timeout_ms)
{
    struct sockaddr_in addr;
    unsigned long mode = 1;
    int ret;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(ip);

    ioctlsocket(s, FIONBIO, &mode);
    ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));

    if (ret == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set wfds;
            struct timeval tv;
            FD_ZERO(&wfds);
            FD_SET(s, &wfds);
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            ret = select(0, NULL, &wfds, NULL, &tv);
            if (ret <= 0) {
                printf("[ERROR] Connection timeout to %s:%d\n", ip, port);
                return -1;
            }
        } else {
            printf("[ERROR] connect() to %s:%d failed: %d\n", ip, port, WSAGetLastError());
            return -1;
        }
    }

    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);
    return 0;
}

/* ================================================================
 * TCP Socket Operations
 * ================================================================ */

int mazak_tcp_connect(MazakConnection *conn, const char *ip, int port)
{
    if (ensure_wsa() != 0) return -1;

    memset(conn, 0, sizeof(*conn));
    strncpy(conn->ip, ip, MAZAK_MAX_IP_LEN - 1);
    conn->ip[MAZAK_MAX_IP_LEN - 1] = 0;
    conn->port = port;
    conn->sock = create_socket();
    if (conn->sock == INVALID_SOCKET) return -1;

    printf("Connecting to Mazak %s:%d ...\n", ip, port);
    if (socket_connect(conn->sock, ip, port, 5000) != 0) {
        closesocket(conn->sock);
        conn->sock = INVALID_SOCKET;
        return -1;
    }

    conn->connected = 1;
    printf("Connected to Mazak %s:%d\n", ip, port);
    return 0;
}

void mazak_tcp_disconnect(MazakConnection *conn)
{
    if (conn->sock != INVALID_SOCKET) {
        shutdown(conn->sock, SD_BOTH);
        closesocket(conn->sock);
        conn->sock = INVALID_SOCKET;
    }
    conn->connected = 0;
    printf("Disconnected from Mazak %s:%d\n", conn->ip, conn->port);
}

int mazak_tcp_send(MazakConnection *conn, const char *data, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = send(conn->sock, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) {
            printf("[ERROR] send() failed: %d\n", WSAGetLastError());
            return -1;
        }
        sent += n;
    }
    return sent;
}

int mazak_tcp_recv(MazakConnection *conn, char *buf, int buf_size, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    int ret;

    FD_ZERO(&rfds);
    FD_SET(conn->sock, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(0, &rfds, NULL, NULL, &tv);
    if (ret == 0) return 0;
    if (ret == SOCKET_ERROR) return -1;

    return recv(conn->sock, buf, buf_size, 0);
}

int mazak_tcp_recv_line(MazakConnection *conn, char *buf, int buf_size, int timeout_ms)
{
    int i = 0;
    char c;
    int n;

    while (i < buf_size - 1) {
        n = mazak_tcp_recv(conn, &c, 1, timeout_ms);
        if (n <= 0) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = 0;
    return i;
}

/* ================================================================
 * Port Scanning
 * ================================================================ */

int mazak_scan_port(const char *ip, int port, int timeout_ms, MazakPortInfo *info)
{
    SOCKET s;
    memset(info, 0, sizeof(*info));
    info->port = port;

    if (ensure_wsa() != 0) return -1;

    s = create_socket();
    if (s == INVALID_SOCKET) return -1;

    if (socket_connect(s, ip, port, timeout_ms) == 0) {
        info->is_open = 1;
        strncpy(info->ip, ip, MAZAK_MAX_IP_LEN - 1);

        char banner[128] = {0};
        int n = recv(s, banner, sizeof(banner) - 1, 2000);
        if (n > 0) {
            banner[n] = 0;
            strncpy(info->banner, banner, sizeof(info->banner) - 1);
        }

        closesocket(s);
        return 1;
    }

    closesocket(s);
    return 0;
}

int mazak_scan_common_ports(const char *ip, int timeout_ms, MazakPortScanResult *result)
{
    int common_ports[] = {
        21,    /* FTP */
        22,    /* SSH */
        23,    /* Telnet */
        80,    /* HTTP */
        443,   /* HTTPS */
        445,   /* SMB */
        502,   /* Modbus TCP */
        161,   /* SNMP */
        102,   /* S7comm (Siemens) */
        7878,  /* MTConnect */
        4840,  /* OPC UA */
        4841,  /* OPC UA alt */
        50100, /* Mazak raw TCP */
        50200, /* Mazak alt TCP */
        8193,  /* FANUC FOCAS2 */
        8194,  /* FANUC FOCAS2 alt */
        9000,  /* Mazak DNC */
        9100,  /* JetDirect */
        2000,  /* Mazak SmartBox */
        3000,  /* Mazak alt */
        10000, /* Common SCADA */
        0
    };
    int i;

    memset(result, 0, sizeof(*result));

    for (i = 0; common_ports[i] != 0 && result->count < 64; i++) {
        if (mazak_scan_port(ip, common_ports[i], timeout_ms,
                            &result->ports[result->count])) {
            result->count++;
        }
    }

    return result->count;
}

void mazak_print_port_scan(const MazakPortScanResult *result)
{
    int i;
    printf("\n--- Mazak Port Scan Results (%d open) ---\n", result->count);
    for (i = 0; i < result->count; i++) {
        const MazakPortInfo *p = &result->ports[i];
        printf("  Port %d: OPEN", p->port);
        if (p->banner[0])
            printf("  Banner: %s", p->banner);
        printf("\n");
    }
    if (result->count == 0)
        printf("  No open ports found\n");
}

/* ================================================================
 * MTConnect Protocol (port 7878)
 * ================================================================ */

int mazak_mtconnect_connect(MazakConnection *conn, const char *ip, int port)
{
    int ret;
    conn->protocol = MAZAK_PROTO_MTCONNECT;
    ret = mazak_tcp_connect(conn, ip, port);
    if (ret == 0) {
        printf("[MTConnect] Connected to %s:%d\n", ip, port);
    }
    return ret;
}

int mazak_mtconnect_probe(MazakConnection *conn)
{
    const char *probe = "MTConnect Probe\n";
    char buf[MAZAK_MAX_BUF_SIZE];
    int n;

    printf("[MTConnect] Sending probe request...\n");
    if (mazak_tcp_send(conn, probe, (int)strlen(probe)) < 0)
        return -1;

    n = mazak_tcp_recv(conn, buf, sizeof(buf) - 1, 5000);
    if (n > 0) {
        buf[n] = 0;
        printf("[MTConnect] Probe response:\n%s\n", buf);
        return 0;
    }
    return -1;
}

int mazak_mtconnect_read(MazakConnection *conn, MazakMTConnectData *data)
{
    const char *cmd = "MTConnect Streams\n";
    char buf[MAZAK_MAX_BUF_SIZE * 4];
    char *p, *line, *tag, *val;
    int n;

    data->count = 0;
    data->capacity = 128;
    data->samples = (MazakMTConnectSample *)calloc(data->capacity,
                                                    sizeof(MazakMTConnectSample));
    if (!data->samples) return -1;

    if (mazak_tcp_send(conn, cmd, (int)strlen(cmd)) < 0)
        return -1;

    n = mazak_tcp_recv(conn, buf, sizeof(buf) - 1, 5000);
    if (n <= 0) return -1;
    buf[n] = 0;

    line = strtok(buf, "\r\n");
    while (line && data->count < data->capacity) {
        if (strstr(line, "|")) {
            tag = strtok(line, "|");
            val = strtok(NULL, "|");
            if (tag && val) {
                MazakMTConnectSample *s = &data->samples[data->count];
                strncpy(s->tag, tag, sizeof(s->tag) - 1);
                strncpy(s->value, val, sizeof(s->value) - 1);
                data->count++;
            }
        }
        line = strtok(NULL, "\r\n");
    }

    return data->count;
}

int mazak_mtconnect_read_changed(MazakConnection *conn, MazakMTConnectData *data)
{
    const char *cmd = "MTConnect Changes\n";
    char buf[MAZAK_MAX_BUF_SIZE * 4];
    char *line, *tag, *val;
    int n;

    data->count = 0;
    data->capacity = 64;
    data->samples = (MazakMTConnectSample *)calloc(data->capacity,
                                                    sizeof(MazakMTConnectSample));
    if (!data->samples) return -1;

    if (mazak_tcp_send(conn, cmd, (int)strlen(cmd)) < 0)
        return -1;

    n = mazak_tcp_recv(conn, buf, sizeof(buf) - 1, 5000);
    if (n <= 0) return -1;
    buf[n] = 0;

    line = strtok(buf, "\r\n");
    while (line && data->count < data->capacity) {
        if (strstr(line, "|")) {
            tag = strtok(line, "|");
            val = strtok(NULL, "|");
            if (tag && val) {
                MazakMTConnectSample *s = &data->samples[data->count];
                strncpy(s->tag, tag, sizeof(s->tag) - 1);
                strncpy(s->value, val, sizeof(s->value) - 1);
                data->count++;
            }
        }
        line = strtok(NULL, "\r\n");
    }

    return data->count;
}

int mazak_mtconnect_ping(MazakConnection *conn)
{
    const char *ping = "* PING\n";
    char buf[64];
    int n;

    if (mazak_tcp_send(conn, ping, (int)strlen(ping)) < 0)
        return -1;

    n = mazak_tcp_recv(conn, buf, sizeof(buf) - 1, 3000);
    if (n > 0) {
        buf[n] = 0;
        if (strstr(buf, "PONG"))
            return 0;
    }
    return -1;
}

void mazak_mtconnect_free_data(MazakMTConnectData *data)
{
    if (data->samples) {
        free(data->samples);
        data->samples = NULL;
    }
    data->count = 0;
    data->capacity = 0;
}

void mazak_print_mtconnect(const MazakMTConnectData *data)
{
    int i;
    printf("\n--- MTConnect Data (%d samples) ---\n", data->count);
    for (i = 0; i < data->count; i++) {
        printf("  %-30s = %s\n", data->samples[i].tag, data->samples[i].value);
    }
    if (data->count == 0)
        printf("  (no data)\n");
}

/* ================================================================
 * FTP Access (port 21)
 * ================================================================ */

static int ftp_send_cmd(MazakConnection *conn, const char *cmd, char *resp, int resp_size)
{
    char buf[MAZAK_MAX_BUF_SIZE];
    int n;

    if (mazak_tcp_send(conn, cmd, (int)strlen(cmd)) < 0)
        return -1;

    n = mazak_tcp_recv_line(conn, resp, resp_size, 5000);
    return n;
}

int mazak_ftp_connect(MazakConnection *conn, const char *ip, int port,
                      const char *user, const char *pass)
{
    char cmd[256], resp[256];

    conn->protocol = MAZAK_PROTO_FTP;
    if (mazak_tcp_connect(conn, ip, port) != 0)
        return -1;

    mazak_tcp_recv(conn, resp, sizeof(resp), 3000);

    _snprintf(cmd, sizeof(cmd), "USER %s\r\n", user);
    ftp_send_cmd(conn, cmd, resp, sizeof(resp));
    printf("[FTP] USER: %s", resp);

    _snprintf(cmd, sizeof(cmd), "PASS %s\r\n", pass);
    ftp_send_cmd(conn, cmd, resp, sizeof(resp));
    printf("[FTP] PASS: %s", resp);

    if (strstr(resp, "230") || strstr(resp, "202"))
        return 0;

    printf("[ERROR] FTP login failed: %s", resp);
    mazak_tcp_disconnect(conn);
    return -1;
}

int mazak_ftp_list(MazakConnection *conn, const char *path, char *buf, int buf_size)
{
    char cmd[256], resp[256];

    ftp_send_cmd(conn, "TYPE A\r\n", resp, sizeof(resp));

    _snprintf(cmd, sizeof(cmd), "CWD %s\r\n", path);
    ftp_send_cmd(conn, cmd, resp, sizeof(resp));

    _snprintf(cmd, sizeof(cmd), "LIST\r\n");
    ftp_send_cmd(conn, cmd, resp, sizeof(resp));

    int n = mazak_tcp_recv(conn, buf, buf_size - 1, 5000);
    if (n > 0) buf[n] = 0;
    else buf[0] = 0;

    ftp_send_cmd(conn, resp, resp, sizeof(resp));
    return n;
}

int mazak_ftp_download(MazakConnection *conn, const char *remote_path,
                       const char *local_path, MazakFTPResult *result)
{
    char cmd[256], resp[256];
    FILE *fp;
    char buf[4096];
    int n;

    memset(result, 0, sizeof(*result));
    strncpy(result->filename, remote_path, sizeof(result->filename) - 1);

    ftp_send_cmd(conn, "TYPE I\r\n", resp, sizeof(resp));

    _snprintf(cmd, sizeof(cmd), "RETR %s\r\n", remote_path);
    ftp_send_cmd(conn, cmd, resp, sizeof(resp));

    if (!strstr(resp, "150")) {
        _snprintf(result->error_msg, sizeof(result->error_msg),
                  "FTP RETR failed: %s", resp);
        return -1;
    }

    fp = fopen(local_path, "wb");
    if (!fp) {
        _snprintf(result->error_msg, sizeof(result->error_msg),
                  "Cannot open local file: %s", local_path);
        return -1;
    }

    while ((n = mazak_tcp_recv(conn, buf, sizeof(buf), 5000)) > 0) {
        fwrite(buf, 1, n, fp);
        result->size += n;
    }
    fclose(fp);

    ftp_send_cmd(conn, resp, resp, sizeof(resp));
    result->success = 1;
    printf("[FTP] Downloaded %s (%ld bytes)\n", remote_path, result->size);
    return 0;
}

int mazak_ftp_upload(MazakConnection *conn, const char *local_path,
                     const char *remote_path, MazakFTPResult *result)
{
    char cmd[256], resp[256];
    FILE *fp;
    char buf[4096];
    int n;

    memset(result, 0, sizeof(*result));

    ftp_send_cmd(conn, "TYPE I\r\n", resp, sizeof(resp));

    _snprintf(cmd, sizeof(cmd), "STOR %s\r\n", remote_path);
    ftp_send_cmd(conn, cmd, resp, sizeof(resp));

    if (!strstr(resp, "150")) {
        _snprintf(result->error_msg, sizeof(result->error_msg),
                  "FTP STOR failed: %s", resp);
        return -1;
    }

    fp = fopen(local_path, "rb");
    if (!fp) {
        _snprintf(result->error_msg, sizeof(result->error_msg),
                  "Cannot open local file: %s", local_path);
        return -1;
    }

    while ((n = (int)fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (mazak_tcp_send(conn, buf, n) < 0) {
            fclose(fp);
            return -1;
        }
        result->size += n;
    }
    fclose(fp);

    ftp_send_cmd(conn, resp, resp, sizeof(resp));
    result->success = 1;
    printf("[FTP] Uploaded %s (%ld bytes)\n", remote_path, result->size);
    return 0;
}

int mazak_ftp_delete(MazakConnection *conn, const char *remote_path)
{
    char cmd[256], resp[256];
    _snprintf(cmd, sizeof(cmd), "DELE %s\r\n", remote_path);
    ftp_send_cmd(conn, cmd, resp, sizeof(resp));
    return strstr(resp, "250") ? 0 : -1;
}

void mazak_print_ftp_result(const MazakFTPResult *result)
{
    if (result->success)
        printf("[FTP] Success: %s (%ld bytes)\n", result->filename, result->size);
    else
        printf("[FTP] Failed: %s\n", result->error_msg);
}

/* ================================================================
 * SMB Access (port 445)
 * ================================================================ */

int mazak_smb_connect(MazakConnection *conn, const char *ip, int port,
                      const char *share, const char *user, const char *pass)
{
    conn->protocol = MAZAK_PROTO_SMB;
    if (mazak_tcp_connect(conn, ip, port) != 0)
        return -1;

    printf("[SMB] Connected to %s:%d share=%s\n", ip, port, share);
    return 0;
}

int mazak_smb_list(MazakConnection *conn, const char *path, char *buf, int buf_size)
{
    (void)conn; (void)path;
    _snprintf(buf, buf_size,
              "[SMB] List requires SMB client library (libsmbclient/smbclient)\n"
              "  Path: %s\n"
              "  Use: smbclient //<ip>/<share> -U <user> -c 'ls <path>'\n", path);
    return (int)strlen(buf);
}

int mazak_smb_read_file(MazakConnection *conn, const char *path,
                        char *buf, int buf_size)
{
    (void)conn; (void)path;
    _snprintf(buf, buf_size,
              "[SMB] Read requires SMB client library\n"
              "  Use: smbclient //<ip>/<share> -U <user> -c 'get <path>'\n");
    return (int)strlen(buf);
}

int mazak_smb_write_file(MazakConnection *conn, const char *path,
                         const char *data, int len)
{
    (void)conn; (void)path; (void)data; (void)len;
    printf("[SMB] Write requires SMB client library\n");
    return -1;
}

/* ================================================================
 * SNMP Query (port 161)
 * ================================================================ */

int mazak_snmp_connect(MazakConnection *conn, const char *ip, int port)
{
    conn->protocol = MAZAK_PROTO_SNMP;
    if (ensure_wsa() != 0) return -1;

    memset(conn, 0, sizeof(*conn));
    strncpy(conn->ip, ip, MAZAK_MAX_IP_LEN - 1);
    conn->port = port;

    conn->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (conn->sock == INVALID_SOCKET) return -1;

    conn->connected = 1;
    printf("[SNMP] Socket opened to %s:%d (UDP)\n", ip, port);
    return 0;
}

int mazak_snmp_get(MazakConnection *conn, const char *oid, MazakSNMPData *data)
{
    unsigned char packet[512];
    int pkt_len = 0;
    int i;

    data->count = 0;
    data->capacity = 32;
    data->vars = (MazakSNMPVariable *)calloc(data->capacity,
                                              sizeof(MazakSNMPVariable));
    if (!data->vars) return -1;

    packet[pkt_len++] = 0x30;
    pkt_len++;
    packet[pkt_len++] = 0x02;
    packet[pkt_len++] = 0x01;
    packet[pkt_len++] = 0x00;
    packet[pkt_len++] = 0xA0;
    pkt_len++;
    packet[pkt_len++] = 0x02;
    packet[pkt_len++] = 0x01;
    packet[pkt_len++] = 0x00;
    packet[pkt_len++] = 0x02;
    packet[pkt_len++] = 0x01;
    packet[pkt_len++] = 0x00;

    const char *dot = oid;
    int oid_len_pos = pkt_len;
    pkt_len++;
    int first = 1;
    while (*dot) {
        int val = 0;
        while (*dot && *dot != '.') dot++;
        if (*dot == '.') dot++;
        while (*dot >= '0' && *dot <= '9') {
            val = val * 10 + (*dot - '0');
            dot++;
        }
        if (first) {
            packet[pkt_len++] = (unsigned char)(val * 40);
            first = 0;
        } else {
            packet[pkt_len++] = (unsigned char)val;
        }
    }
    packet[oid_len_pos] = (unsigned char)(pkt_len - oid_len_pos - 1);

    int total = pkt_len + 2;
    packet[1] = (unsigned char)(pkt_len);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)conn->port);
    addr.sin_addr.s_addr = inet_addr(conn->ip);

    sendto(conn->sock, (char *)packet, pkt_len, 0,
           (struct sockaddr *)&addr, sizeof(addr));

    unsigned char resp[1024];
    int from_len = sizeof(addr);
    int n = recvfrom(conn->sock, (char *)resp, sizeof(resp), 0,
                     (struct sockaddr *)&addr, &from_len);

    if (n > 0 && resp[0] == 0x30) {
        MazakSNMPVariable *v = &data->vars[data->count];
        strncpy(v->name, oid, sizeof(v->name) - 1);
        _snprintf(v->value, sizeof(v->value), "(%d bytes response)", n);
        data->count++;
    }

    return data->count;
}

int mazak_snmp_get_all(MazakConnection *conn, MazakSNMPData *data)
{
    const char *oids[] = {
        "1.3.6.1.2.1.1.1.0",
        "1.3.6.1.2.1.1.2.0",
        "1.3.6.1.2.1.1.3.0",
        "1.3.6.1.2.1.1.4.0",
        "1.3.6.1.2.1.1.5.0",
        "1.3.6.1.2.1.1.6.0",
        NULL
    };
    int i;

    data->count = 0;
    data->capacity = 64;
    data->vars = (MazakSNMPVariable *)calloc(data->capacity,
                                              sizeof(MazakSNMPVariable));
    if (!data->vars) return -1;

    for (i = 0; oids[i]; i++) {
        MazakSNMPData single = {0};
        if (mazak_snmp_get(conn, oids[i], &single) > 0) {
            if (data->count < data->capacity) {
                data->vars[data->count] = single.vars[0];
                data->count++;
            }
            free(single.vars);
        }
    }

    return data->count;
}

void mazak_snmp_free_data(MazakSNMPData *data)
{
    if (data->vars) {
        free(data->vars);
        data->vars = NULL;
    }
    data->count = 0;
    data->capacity = 0;
}

void mazak_print_snmp(const MazakSNMPData *data)
{
    int i;
    printf("\n--- SNMP Data (%d variables) ---\n", data->count);
    for (i = 0; i < data->count; i++) {
        printf("  %-30s = %s\n", data->vars[i].name, data->vars[i].value);
    }
    if (data->count == 0)
        printf("  (no data)\n");
}

/* ================================================================
 * Raw TCP Socket Polling (port 50100)
 * ================================================================ */

int mazak_raw_tcp_connect(MazakConnection *conn, const char *ip, int port)
{
    conn->protocol = MAZAK_PROTO_RAW_TCP;
    return mazak_tcp_connect(conn, ip, port);
}

int mazak_raw_tcp_read(MazakConnection *conn, char *buf, int buf_size, int timeout_ms)
{
    return mazak_tcp_recv(conn, buf, buf_size, timeout_ms);
}

int mazak_raw_tcp_read_line(MazakConnection *conn, char *buf, int buf_size)
{
    return mazak_tcp_recv_line(conn, buf, buf_size, 5000);
}

int mazak_raw_tcp_read_until(MazakConnection *conn, char *buf, int buf_size,
                             const char *terminator, int timeout_ms)
{
    int i = 0;
    int term_len = (int)strlen(terminator);
    char c;

    while (i < buf_size - 1) {
        if (mazak_tcp_recv(conn, &c, 1, timeout_ms) <= 0)
            break;
        buf[i++] = c;
        if (i >= term_len && memcmp(&buf[i - term_len], terminator, term_len) == 0)
            break;
    }
    buf[i] = 0;
    return i;
}

int mazak_raw_tcp_write(MazakConnection *conn, const char *data, int len)
{
    return mazak_tcp_send(conn, data, len);
}

/* ================================================================
 * OPC UA Discovery (port 4840)
 * ================================================================ */

int mazak_opcua_discover(const char *ip, int port, char *buf, int buf_size)
{
    MazakConnection conn;
    const char *request =
        "POST / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/xml\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    char req[512];
    int n;

    _snprintf(req, sizeof(req), request, ip, port);

    if (mazak_tcp_connect(&conn, ip, port) != 0) {
        _snprintf(buf, buf_size, "[OPC UA] Cannot connect to %s:%d\n", ip, port);
        return -1;
    }

    mazak_tcp_send(&conn, req, (int)strlen(req));
    n = mazak_tcp_recv(&conn, buf, buf_size - 1, 5000);
    if (n > 0) buf[n] = 0;
    else _snprintf(buf, buf_size, "[OPC UA] No response from %s:%d\n", ip, port);

    mazak_tcp_disconnect(&conn);
    return n;
}

/* ================================================================
 * Utility Functions
 * ================================================================ */

const char* mazak_protocol_name(MazakProtocol proto)
{
    switch (proto) {
    case MAZAK_PROTO_MTCONNECT: return "MTConnect";
    case MAZAK_PROTO_FTP:       return "FTP";
    case MAZAK_PROTO_SMB:       return "SMB";
    case MAZAK_PROTO_SNMP:      return "SNMP";
    case MAZAK_PROTO_OPCUA:     return "OPC UA";
    case MAZAK_PROTO_RAW_TCP:   return "Raw TCP";
    case MAZAK_PROTO_TCP_PROBE: return "TCP Probe";
    default:                    return "Unknown";
    }
}

void mazak_print_connection(const MazakConnection *conn)
{
    printf("[Mazak] %s:%d  Protocol: %s  Connected: %s\n",
           conn->ip, conn->port,
           mazak_protocol_name(conn->protocol),
           conn->connected ? "YES" : "NO");
}

int mazak_test_connection(const char *ip, int port, int timeout_ms)
{
    MazakConnection conn;
    int ret;

    ret = mazak_tcp_connect(&conn, ip, port);
    if (ret == 0) {
        printf("[TEST] %s:%d - OK\n", ip, port);
        mazak_tcp_disconnect(&conn);
        return 1;
    }
    printf("[TEST] %s:%d - FAILED\n", ip, port);
    return 0;
}
