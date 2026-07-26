#ifndef MAZAK_OPS_H
#define MAZAK_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#define MAZAK_DEFAULT_MTCONNECT_PORT   7878
#define MAZAK_DEFAULT_FTP_PORT         21
#define MAZAK_DEFAULT_SMB_PORT         445
#define MAZAK_DEFAULT_SNMP_PORT        161
#define MAZAK_DEFAULT_OPCUA_PORT       4840
#define MAZAK_DEFAULT_RAW_TCP_PORT     50100
#define MAZAK_MAX_BUF_SIZE             4096
#define MAZAK_MAX_IP_LEN              64
#define MAZAK_MTCONNECT_PING_INTERVAL  10000

typedef enum {
    MAZAK_PROTO_UNKNOWN = 0,
    MAZAK_PROTO_MTCONNECT,
    MAZAK_PROTO_FTP,
    MAZAK_PROTO_SMB,
    MAZAK_PROTO_SNMP,
    MAZAK_PROTO_OPCUA,
    MAZAK_PROTO_RAW_TCP,
    MAZAK_PROTO_TCP_PROBE
} MazakProtocol;

typedef struct {
    char ip[MAZAK_MAX_IP_LEN];
    int  port;
    int  connected;
    SOCKET sock;
    MazakProtocol protocol;
} MazakConnection;

typedef struct {
    char timestamp[64];
    char tag[128];
    char value[256];
} MazakMTConnectSample;

typedef struct {
    MazakMTConnectSample *samples;
    int count;
    int capacity;
} MazakMTConnectData;

typedef struct {
    int  is_open;
    char ip[MAZAK_MAX_IP_LEN];
    int  port;
    char service[32];
    char banner[128];
} MazakPortInfo;

typedef struct {
    MazakPortInfo ports[64];
    int count;
} MazakPortScanResult;

typedef struct {
    char name[128];
    char value[256];
    char timestamp[64];
} MazakSNMPVariable;

typedef struct {
    MazakSNMPVariable *vars;
    int count;
    int capacity;
} MazakSNMPData;

typedef struct {
    int  success;
    char filename[256];
    long size;
    char error_msg[128];
} MazakFTPResult;

typedef struct {
    int  success;
    char error_msg[128];
} MazakGenericResult;

/* TCP Socket Operations */
int  mazak_tcp_connect(MazakConnection *conn, const char *ip, int port);
void mazak_tcp_disconnect(MazakConnection *conn);
int  mazak_tcp_send(MazakConnection *conn, const char *data, int len);
int  mazak_tcp_recv(MazakConnection *conn, char *buf, int buf_size, int timeout_ms);
int  mazak_tcp_recv_line(MazakConnection *conn, char *buf, int buf_size, int timeout_ms);

/* Port Scanning */
int  mazak_scan_port(const char *ip, int port, int timeout_ms, MazakPortInfo *info);
int  mazak_scan_common_ports(const char *ip, int timeout_ms, MazakPortScanResult *result);
void mazak_print_port_scan(const MazakPortScanResult *result);

/* MTConnect Protocol (port 7878) */
int  mazak_mtconnect_connect(MazakConnection *conn, const char *ip, int port);
int  mazak_mtconnect_probe(MazakConnection *conn);
int  mazak_mtconnect_read(MazakConnection *conn, MazakMTConnectData *data);
int  mazak_mtconnect_read_changed(MazakConnection *conn, MazakMTConnectData *data);
int  mazak_mtconnect_ping(MazakConnection *conn);
void mazak_mtconnect_free_data(MazakMTConnectData *data);
void mazak_print_mtconnect(const MazakMTConnectData *data);

/* FTP Access (port 21) */
int  mazak_ftp_connect(MazakConnection *conn, const char *ip, int port,
                       const char *user, const char *pass);
int  mazak_ftp_list(MazakConnection *conn, const char *path, char *buf, int buf_size);
int  mazak_ftp_download(MazakConnection *conn, const char *remote_path,
                        const char *local_path, MazakFTPResult *result);
int  mazak_ftp_upload(MazakConnection *conn, const char *local_path,
                      const char *remote_path, MazakFTPResult *result);
int  mazak_ftp_delete(MazakConnection *conn, const char *remote_path);
void mazak_print_ftp_result(const MazakFTPResult *result);

/* SMB Access (port 445) */
int  mazak_smb_connect(MazakConnection *conn, const char *ip, int port,
                       const char *share, const char *user, const char *pass);
int  mazak_smb_list(MazakConnection *conn, const char *path, char *buf, int buf_size);
int  mazak_smb_read_file(MazakConnection *conn, const char *path,
                         char *buf, int buf_size);
int  mazak_smb_write_file(MazakConnection *conn, const char *path,
                          const char *data, int len);

/* SNMP Query (port 161) */
int  mazak_snmp_connect(MazakConnection *conn, const char *ip, int port);
int  mazak_snmp_get(MazakConnection *conn, const char *oid, MazakSNMPData *data);
int  mazak_snmp_get_all(MazakConnection *conn, MazakSNMPData *data);
void mazak_snmp_free_data(MazakSNMPData *data);
void mazak_print_snmp(const MazakSNMPData *data);

/* Raw TCP Socket Polling (port 50100) */
int  mazak_raw_tcp_connect(MazakConnection *conn, const char *ip, int port);
int  mazak_raw_tcp_read(MazakConnection *conn, char *buf, int buf_size, int timeout_ms);
int  mazak_raw_tcp_read_line(MazakConnection *conn, char *buf, int buf_size);
int  mazak_raw_tcp_read_until(MazakConnection *conn, char *buf, int buf_size,
                              const char *terminator, int timeout_ms);
int  mazak_raw_tcp_write(MazakConnection *conn, const char *data, int len);

/* OPC UA Discovery (port 4840) */
int  mazak_opcua_discover(const char *ip, int port, char *buf, int buf_size);

/* Utility Functions */
const char* mazak_protocol_name(MazakProtocol proto);
void mazak_print_connection(const MazakConnection *conn);
int  mazak_test_connection(const char *ip, int port, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
