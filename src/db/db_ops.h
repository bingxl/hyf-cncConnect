#ifndef DB_OPS_H
#define DB_OPS_H

#define DB_MAX_MACHINES 64
#define DB_MAX_NAME     64
#define DB_MAX_IP       64
#define DB_MAX_TIME     32
#define DB_MAX_COMMENT  64
#define DB_MAX_SNAPSHOT 8192

typedef struct {
    int id;
    char name[DB_MAX_NAME];
    char ip[DB_MAX_IP];
    int port;
} MachineRecord;

typedef struct {
    int id;
    int machine_id;
    char name[DB_MAX_NAME];
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

typedef struct {
    int machine_id;
    long ts;                 /* unix epoch seconds of the sample */
    int run;                 /* 1 = running */
    int aut;                 /* 1 = auto mode */
    int tmmode;              /* 0 = memory mode */
    int program_no;
    char program_comment[DB_MAX_COMMENT];
    long part_total;
    long part_current;
    long part_required;
} MachineSample;

typedef struct {
    int machine_id;
    long ts;
    int run;
    int aut;
    int tmmode;
    int program_no;
    char program_comment[DB_MAX_COMMENT];
    long part_total;
    long part_current;
    long part_required;
} MachineLatest;

typedef struct {
    long ts_bucket;          /* aligned start of the 30-min bucket */
    long ts_begin;           /* first sample time in bucket */
    long ts_end;             /* last sample time in bucket */
    int  machining_count;    /* samples machining within bucket */
                             /* machining == run==3 (STaRT) && aut==1 (MEM)
                                && tmmode==0 (T/lathe mode) */
                             /* count * sample interval -> machining seconds */
    int  sample_count;
    long part_begin;
    long part_end;
    long produced;           /* part_end - part_begin (>=0) */
} MachineBucket;

typedef struct {
    int machine_id;
    char program_comment[DB_MAX_COMMENT];
    int  machining_count;    /* samples machining within range (per machine) */
    long produced;
    long rows;
} ProductGroup;

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

int db_get_schema_version(DbHandle db);
int db_set_schema_version(DbHandle db, int version);

int db_delete_batch(DbHandle db, int batch_id);
int db_delete_history_entry(DbHandle db, int id);
int db_update_history_entry(DbHandle db, int id, long required, long current, long total);
int db_get_batch_count(DbHandle db);
int db_get_batches_paged(DbHandle db, int offset, int limit, BatchInfo *list);

/* --- machining-time / product statistics sampling tables --- */

int db_add_sample(DbHandle db, const MachineSample *s);
int db_add_sample_bulk(DbHandle db, const MachineSample *s, int count);
int db_upsert_machine_latest(DbHandle db, const MachineLatest *l);

int db_get_samples(DbHandle db, int machine_id, long t0, long t1,
                   MachineSample *list, int max_count);
int db_get_samples_count(DbHandle db, int machine_id, long t0, long t1);

/* time-series buckets (machine working time), ordered by ts ascending */
int db_get_buckets(DbHandle db, int machine_id, long t0, long t1,
                   MachineBucket *list, int max_count);

/* product (program_comment) grouping within a time range, per machine */
int db_get_products(DbHandle db, int machine_id, long t0, long t1,
                    ProductGroup *list, int max_count);

int db_set_pruned_before(DbHandle db, long ts);
int db_get_pruned_before(DbHandle db);

int db_set_meta_int(DbHandle db, const char *key, long value);
long db_get_meta_int(DbHandle db, const char *key);
int db_get_sample_time_range(DbHandle db, int machine_id, long *tmin, long *tmax);

/* Move samples with ts < cutoff into the archive database file whose path
   is archive_path (attached as "archive"), then delete them from the main
   db in a single transaction. Returns number of rows archived, -1 on error. */
long db_archive_samples_older_than(DbHandle db, const char *archive_path, long cutoff);
long db_count_samples_older_than(DbHandle db, long cutoff);

#ifdef __cplusplus
}
#endif

#endif
