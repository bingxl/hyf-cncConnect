#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mazak_ops.h"

#define DEFAULT_PORT      MAZAK_DEFAULT_RAW_TCP_PORT
#define DEFAULT_TIMEOUT   5000

static void print_banner(void)
{
    printf("============================================\n");
    printf("  Mazak Machine Test Tool\n");
    printf("  Tests connectivity and data access\n");
    printf("============================================\n\n");
}

static void print_usage(const char *prog)
{
    printf("Usage: %s <IP> [options]\n\n", prog);
    printf("Options:\n");
    printf("  <port>            TCP port (default: %d, Raw TCP)\n", DEFAULT_PORT);
    printf("  -raw              Raw TCP protocol test (default)\n");
    printf("  -mtconnect        MTConnect protocol test (port 7878)\n");
    printf("  -scan             Scan common ports\n");
    printf("  -all              Run all tests\n");
    printf("  -timeout <ms>     Connection timeout (default: %dms)\n", DEFAULT_TIMEOUT);
    printf("\nExamples:\n");
    printf("  %s 192.168.1.100              (Raw TCP default)\n", prog);
    printf("  %s 192.168.1.100 -all         (all tests)\n", prog);
    printf("  %s 192.168.1.100 -scan        (port scan only)\n", prog);
    printf("  %s 192.168.1.100 7878 -mtconnect\n", prog);
}

/* ---- output helpers ---- */

static void print_safe_text(const char *data, int len, const char *indent)
{
    int line_start = 1;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\r') continue;
        if (c == '\n') {
            printf("\n");
            line_start = 1;
        } else {
            if (line_start) {
                printf("%s", indent);
                line_start = 0;
            }
            if (c >= 32 && c < 127)
                putchar(c);
            else
                putchar('.');
        }
    }
    if (!line_start) printf("\n");
}

static void print_hex_dump(const char *label, const char *data, int len)
{
    printf("  %s (%d bytes):\n", label, len);

    for (int i = 0; i < len; i += 16) {
        printf("    %04x  ", i);

        for (int j = 0; j < 16; j++) {
            if (i + j < len)
                printf("%02x ", (unsigned char)data[i + j]);
            else
                printf("   ");
            if (j == 7) printf(" ");
        }

        printf(" ");
        for (int j = 0; j < 16 && i + j < len; j++) {
            unsigned char c = (unsigned char)data[i + j];
            printf("%c", c >= 32 && c < 127 ? c : '.');
        }
        printf("\n");
    }
}

static void show_data(const char *label, const char *data, int len)
{
    printf("  %s (%d 字节):\n", label, len);
    printf("  |--- ASCII ---\n");
    print_safe_text(data, len, "  ");
    print_hex_dump("  |--- HEX", data, len);
}

/* ---- Raw TCP Test ---- */

static const char *raw_queries[] = {
    "STATUS\n",
    "INFO\n",
    "STATE\n",
    "MODE\n",
    "POSITION\n",
    "POS\n",
    "ALARM\n",
    "TOOL\n",
    "SPINDLE\n",
    "FEED\n",
    "PROGRAM\n",
    "HELP\n",
    "?\n",
    NULL
};

static int test_raw_tcp(const char *ip, int port, int timeout_ms)
{
    MazakConnection conn;
    char buf[MAZAK_MAX_BUF_SIZE * 4];
    int n;

    printf("\n=== Raw TCP 测试 ===\n");

    printf("\n[1/4] 连接 %s:%d ...\n", ip, port);
    if (mazak_raw_tcp_connect(&conn, ip, port) != 0) {
        printf("  -> 连接失败\n");
        return -1;
    }
    printf("  -> 连接成功\n");

    printf("\n[2/4] 读取初始数据 ...\n");
    int total = 0;
    for (int i = 0; i < 3; i++) {
        n = mazak_tcp_recv(&conn, buf + total, sizeof(buf) - total - 1, 1000);
        if (n > 0) {
            total += n;
            buf[total] = 0;
            printf("  第 %d 次读取: %d 字节\n", i + 1, n);
        } else {
            break;
        }
    }

    if (total > 0) {
        show_data("初始数据", buf, total);
    } else {
        printf("  -> 无初始数据\n");
    }

    printf("\n[3/4] 发送查询命令并读取响应 ...\n");
    int found_response = 0;
    for (int qi = 0; raw_queries[qi]; qi++) {
        memset(buf, 0, sizeof(buf));

        if (mazak_raw_tcp_write(&conn, raw_queries[qi],
                                (int)strlen(raw_queries[qi])) < 0) {
            printf("  [WARN] 发送查询失败\n");
            continue;
        }

        n = mazak_tcp_recv(&conn, buf, sizeof(buf) - 1, 1500);
        if (n > 0) {
            buf[n] = 0;
            found_response = 1;
            char qbuf[64];
            size_t qlen = strlen(raw_queries[qi]);
            if (qlen > 0 && raw_queries[qi][qlen - 1] == '\n')
                qlen--;
            memcpy(qbuf, raw_queries[qi], qlen);
            qbuf[qlen] = 0;

            int is_text = 1;
            for (int i = 0; i < n && is_text; i++)
                if (buf[i] != 0 && buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t'
                    && (buf[i] < 32 || buf[i] > 126))
                    is_text = 0;

            printf("  >> %s\n", qbuf);
            show_data("    响应", buf, n);
        }
    }

    if (!found_response) {
        printf("  -> 所有查询均无响应\n");
    }

    printf("\n[4/4] 持续读取（2 秒）...\n");
    total = 0;
    for (int i = 0; i < 4; i++) {
        n = mazak_tcp_recv(&conn, buf + total, sizeof(buf) - total - 1, 500);
        if (n > 0) {
            total += n;
            buf[total] = 0;
        }
    }
    if (total > 0) {
        printf("  收到额外 %d 字节\n", total);
        show_data("  数据", buf, total);
    } else {
        printf("  无更多数据\n");
    }

    mazak_tcp_disconnect(&conn);
    return 0;
}

/* ---- MTConnect Test ---- */

static int test_mtconnect(const char *ip, int port, int timeout_ms)
{
    MazakConnection conn;
    MazakMTConnectData data;
    int ret = 0;

    printf("\n=== MTConnect 测试 ===\n");

    printf("\n[1/3] 连接 ...\n");
    if (mazak_mtconnect_connect(&conn, ip, port) != 0) {
        printf("  -> 连接失败\n");
        return -1;
    }

    printf("\n[2/3] 探测设备 ...\n");
    if (mazak_mtconnect_probe(&conn) == 0) {
        printf("  -> 探测成功\n");
    } else {
        printf("  -> 探测失败\n");
    }

    printf("\n[3/3] 读取数据 ...\n");
    memset(&data, 0, sizeof(data));
    int n = mazak_mtconnect_read(&conn, &data);
    if (n > 0) {
        printf("  -> 读取到 %d 个数据项\n", n);
        for (int i = 0; i < data.count; i++)
            printf("  %-30s = %s\n", data.samples[i].tag, data.samples[i].value);
        mazak_mtconnect_free_data(&data);
        ret = 0;
    } else {
        printf("  -> 未读取到数据\n");
        ret = -1;
    }

    mazak_tcp_disconnect(&conn);
    return ret;
}

/* ---- Port Scan ---- */

static int test_port_scan(const char *ip, int timeout_ms)
{
    MazakPortScanResult result;

    printf("\n=== 端口扫描 ===\n");
    printf("扫描 %s ...\n", ip);

    int n = mazak_scan_common_ports(ip, timeout_ms, &result);
    if (n > 0) {
        printf("发现 %d 个开放端口\n", n);
        mazak_print_port_scan(&result);
    } else {
        printf("未发现开放端口\n");
    }

    return n;
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    const char *ip;
    int port = DEFAULT_PORT;
    int timeout_ms = DEFAULT_TIMEOUT;
    int do_raw = 1;
    int do_mtconnect = 0;
    int do_scan = 0;
    int all_tests = 0;
    int i;

    SetConsoleOutputCP(CP_UTF8);

    print_banner();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    ip = argv[1];

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-raw") == 0) {
            do_raw = 1;
            if (port == DEFAULT_PORT)
                port = MAZAK_DEFAULT_RAW_TCP_PORT;
        } else if (strcmp(argv[i], "-mtconnect") == 0) {
            do_mtconnect = 1;
            do_raw = 0;
            if (port == DEFAULT_PORT)
                port = MAZAK_DEFAULT_MTCONNECT_PORT;
        } else if (strcmp(argv[i], "-scan") == 0) {
            do_scan = 1;
            do_raw = 0;
        } else if (strcmp(argv[i], "-all") == 0) {
            all_tests = 1;
            do_raw = 1;
            do_mtconnect = 1;
            do_scan = 1;
        } else if (strcmp(argv[i], "-timeout") == 0) {
            if (i + 1 < argc) {
                timeout_ms = atoi(argv[i + 1]);
                if (timeout_ms <= 0) timeout_ms = DEFAULT_TIMEOUT;
                i++;
            }
        } else if (port == DEFAULT_PORT) {
            port = atoi(argv[i]);
            if (port <= 0 || port > 65535) {
                printf("[ERROR] Invalid port: %s\n", argv[i]);
                return 1;
            }
        }
    }

    printf("目标: %s:%d  超时: %dms\n\n", ip, port, timeout_ms);

    int passed = 0;
    int failed = 0;

    printf("=== 基本连通测试 ===\n");
    if (mazak_test_connection(ip, port, timeout_ms))
        passed++;
    else
        failed++;
    printf("\n");

    if (do_scan || all_tests) {
        if (test_port_scan(ip, timeout_ms) >= 0)
            passed++;
        else
            failed++;
        printf("\n");
    }

    if (do_raw) {
        if (test_raw_tcp(ip, port, timeout_ms) == 0)
            passed++;
        else
            failed++;
        printf("\n");
    }

    if (do_mtconnect || all_tests) {
        int mt_port = all_tests ? MAZAK_DEFAULT_MTCONNECT_PORT : port;
        if (test_mtconnect(ip, mt_port, timeout_ms) == 0)
            passed++;
        else
            failed++;
        printf("\n");
    }

    printf("============================================\n");
    printf("  测试完成: %d 通过, %d 失败\n", passed, failed);
    printf("============================================\n");

    return failed > 0 ? 1 : 0;
}
