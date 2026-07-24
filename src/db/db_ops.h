#ifndef DB_OPS_H
#define DB_OPS_H

#define DB_MAX_MACHINES 64
#define DB_MAX_NAME     64
#define DB_MAX_IP       64
#define DB_MAX_TIME     32

typedef struct {
    int id;
    char name[DB_MAX_NAME];
    char ip[DB_MAX_IP];
    int port;
} MachineRecord;

typedef struct {
    int id;
    int machine_id;
    char save_time[DB_MAX_TIME];
    long required;
    long current;
    long total;
    int batch_id;
} HistoryRecord;

typedef struct {
    int batch_id;
    char save_time[DB_MAX_TIME];
} BatchInfo;

typedef void *DbHandle;

#ifdef __cplusplus
extern "C" {
#endif

DbHandle db_open(const char *db_path);
void db_close(DbHandle db);

int db_init_tables(DbHandle db);

int db_add_machine(DbHandle db, const char *name, const char *ip, int port);
int db_update_machine(DbHandle db, int id, const char *name, const char *ip, int port);
int db_delete_machine(DbHandle db, int id);
int db_get_machines(DbHandle db, MachineRecord *list, int max_count);
int db_get_machine_count(DbHandle db);
int db_get_machine_by_id(DbHandle db, int id, MachineRecord *rec);

int db_save_history(DbHandle db, int machine_id, long required, long current, long total);
int db_save_batch(DbHandle db, HistoryRecord *records, int count);
int db_get_latest_batch_id(DbHandle db);
int db_get_batch_list(DbHandle db, BatchInfo *list, int max_count);
int db_get_batch_history(DbHandle db, int batch_id, HistoryRecord *list, int max_count);

#ifdef __cplusplus
}
#endif

#endif
