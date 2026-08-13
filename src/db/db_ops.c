#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "sqlite3.h"
#include "db_ops.h"

struct DbHandle {
    sqlite3 *db;
};

static struct DbHandle *cast_handle(DbHandle db)
{
    return (struct DbHandle *)db;
}

DbHandle db_open(const char *db_path)
{
    struct DbHandle *h;
    int rc;
    if (!db_path) return NULL;
    h = (struct DbHandle *)malloc(sizeof(struct DbHandle));
    if (!h) return NULL;
    rc = sqlite3_open(db_path, &h->db);
    if (rc != SQLITE_OK) {
        free(h);
        return NULL;
    }
    sqlite3_exec(h->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    /* Wait up to 5s for a busy DB (e.g. GUI/sampler writing concurrently)
       instead of failing immediately with SQLITE_BUSY. */
    sqlite3_exec(h->db, "PRAGMA busy_timeout=5000;", NULL, NULL, NULL);
    return (DbHandle)h;
}

void db_close(DbHandle db)
{
    struct DbHandle *h = cast_handle(db);
    if (!h) return;
    sqlite3_close(h->db);
    free(h);
}

int db_init_tables(DbHandle db)
{
    struct DbHandle *h = cast_handle(db);
    const char *sql =
        "CREATE TABLE IF NOT EXISTS machines ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  ip   TEXT NOT NULL,"
        "  port INTEGER NOT NULL DEFAULT 8193"
        ");"
        "CREATE TABLE IF NOT EXISTS history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  machine_id INTEGER NOT NULL,"
        "  save_time  TEXT NOT NULL,"
        "  required   INTEGER DEFAULT 0,"
        "  current    INTEGER DEFAULT 0,"
        "  total      INTEGER DEFAULT 0,"
        "  batch_id   INTEGER DEFAULT 0,"
        "  FOREIGN KEY(machine_id) REFERENCES machines(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS machine_samples ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  machine_id       INTEGER NOT NULL,"
        "  ts               INTEGER NOT NULL,"
        "  run              INTEGER NOT NULL DEFAULT 0,"
        "  aut              INTEGER NOT NULL DEFAULT 0,"
        "  tmmode           INTEGER NOT NULL DEFAULT 0,"
        "  program_no       INTEGER NOT NULL DEFAULT 0,"
        "  program_comment  TEXT NOT NULL DEFAULT '',"
        "  part_total       INTEGER NOT NULL DEFAULT 0,"
        "  part_current     INTEGER NOT NULL DEFAULT 0,"
        "  part_required    INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_samples_mach_ts "
        "  ON machine_samples(machine_id, ts);"
        "CREATE TABLE IF NOT EXISTS machine_latest ("
        "  machine_id       INTEGER PRIMARY KEY,"
        "  ts               INTEGER NOT NULL,"
        "  run              INTEGER NOT NULL DEFAULT 0,"
        "  aut              INTEGER NOT NULL DEFAULT 0,"
        "  tmmode           INTEGER NOT NULL DEFAULT 0,"
        "  program_no       INTEGER NOT NULL DEFAULT 0,"
        "  program_comment  TEXT NOT NULL DEFAULT '',"
        "  part_total       INTEGER NOT NULL DEFAULT 0,"
        "  part_current     INTEGER NOT NULL DEFAULT 0,"
        "  part_required    INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS _meta (key TEXT PRIMARY KEY, value INTEGER);";
    char *errmsg = NULL;
    int rc;
    if (!h) return -1;
    rc = sqlite3_exec(h->db, sql, NULL, NULL, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    return (rc == SQLITE_OK) ? 0 : -1;
}

int db_add_machine(DbHandle db, const char *name, const char *ip, int port)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h || !name || !ip) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "INSERT INTO machines (name, ip, port) VALUES (?, ?, ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ip, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, port);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_update_machine(DbHandle db, int id, const char *name, const char *ip, int port)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h || !name || !ip) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "UPDATE machines SET name=?, ip=?, port=? WHERE id=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ip, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, port);
    sqlite3_bind_int(stmt, 4, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_delete_machine(DbHandle db, int id)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "DELETE FROM machines WHERE id=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_machines(DbHandle db, MachineRecord *list, int max_count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, n = 0;
    if (!h || !list || max_count <= 0) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT id, name, ip, port FROM machines ORDER BY id",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    while (n < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        list[n].id = sqlite3_column_int(stmt, 0);
        strncpy(list[n].name, (const char *)sqlite3_column_text(stmt, 1), DB_MAX_NAME - 1);
        strncpy(list[n].ip, (const char *)sqlite3_column_text(stmt, 2), DB_MAX_IP - 1);
        list[n].port = sqlite3_column_int(stmt, 3);
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

int db_get_machine_count(DbHandle db)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, count = 0;
    if (!h) return 0;
    rc = sqlite3_prepare_v2(h->db, "SELECT COUNT(*) FROM machines", -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int db_get_machine_by_id(DbHandle db, int id, MachineRecord *rec)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h || !rec) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT id, name, ip, port FROM machines WHERE id=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        rec->id = sqlite3_column_int(stmt, 0);
        strncpy(rec->name, (const char *)sqlite3_column_text(stmt, 1), DB_MAX_NAME - 1);
        strncpy(rec->ip, (const char *)sqlite3_column_text(stmt, 2), DB_MAX_IP - 1);
        rec->port = sqlite3_column_int(stmt, 3);
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    return -1;
}

int db_save_batch(DbHandle db, HistoryRecord *records, int count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, batch_id, i;
    time_t now;
    struct tm *tm_now;
    char time_str[DB_MAX_TIME];

    if (!h || !records || count <= 0) return -1;

    batch_id = db_get_latest_batch_id(db) + 1;

    now = time(NULL);
    tm_now = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_now);

    rc = sqlite3_prepare_v2(h->db,
        "INSERT INTO history (machine_id, save_time, required, current, total, batch_id) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    for (i = 0; i < count; i++) {
        sqlite3_bind_int(stmt, 1, records[i].machine_id);
        sqlite3_bind_text(stmt, 2, time_str, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, (int)records[i].required);
        sqlite3_bind_int(stmt, 4, (int)records[i].current);
        sqlite3_bind_int(stmt, 5, (int)records[i].total);
        sqlite3_bind_int(stmt, 6, batch_id);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    return batch_id;
}

int db_get_latest_batch_id(DbHandle db)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, batch_id = 0;
    if (!h) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT MAX(batch_id) FROM history", -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        batch_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return batch_id;
}

int db_get_batch_list(DbHandle db, BatchInfo *list, int max_count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, n = 0;
    if (!h || !list || max_count <= 0) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT DISTINCT batch_id, save_time FROM history "
        "ORDER BY batch_id DESC LIMIT ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, max_count);
    while (n < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        list[n].batch_id = sqlite3_column_int(stmt, 0);
        strncpy(list[n].save_time, (const char *)sqlite3_column_text(stmt, 1), DB_MAX_TIME - 1);
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

int db_get_batch_history(DbHandle db, int batch_id, HistoryRecord *list, int max_count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, n = 0;
    if (!h || !list || max_count <= 0) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT h.id, h.machine_id, m.name, h.save_time, h.required, h.current, h.total, h.batch_id "
        "FROM history h JOIN machines m ON h.machine_id = m.id "
        "WHERE h.batch_id = ? ORDER BY m.id",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, batch_id);
    while (n < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        list[n].id = sqlite3_column_int(stmt, 0);
        list[n].machine_id = sqlite3_column_int(stmt, 1);
        strncpy(list[n].name, (const char *)sqlite3_column_text(stmt, 2), DB_MAX_NAME - 1);
        strncpy(list[n].save_time, (const char *)sqlite3_column_text(stmt, 3), DB_MAX_TIME - 1);
        list[n].required = sqlite3_column_int(stmt, 4);
        list[n].current = sqlite3_column_int(stmt, 5);
        list[n].total = sqlite3_column_int(stmt, 6);
        list[n].batch_id = sqlite3_column_int(stmt, 7);
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

int db_get_schema_version(DbHandle db)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, version = 0;
    if (!h) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "CREATE TABLE IF NOT EXISTS _meta (key TEXT PRIMARY KEY, value INTEGER)", -1, &stmt, NULL);
    if (rc == SQLITE_OK) { sqlite3_step(stmt); sqlite3_finalize(stmt); }
    rc = sqlite3_prepare_v2(h->db,
        "SELECT value FROM _meta WHERE key='schema_version'", -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return version;
}

int db_set_schema_version(DbHandle db, int version)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "INSERT OR REPLACE INTO _meta (key, value) VALUES ('schema_version', ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, version);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_delete_batch(DbHandle db, int batch_id)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "DELETE FROM history WHERE batch_id=?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, batch_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_delete_history_entry(DbHandle db, int id)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "DELETE FROM history WHERE id=?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_update_history_entry(DbHandle db, int id, long required, long current, long total)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "UPDATE history SET required=?, current=?, total=? WHERE id=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)required);
    sqlite3_bind_int(stmt, 2, (int)current);
    sqlite3_bind_int(stmt, 3, (int)total);
    sqlite3_bind_int(stmt, 4, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_batch_count(DbHandle db)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, count = 0;
    if (!h) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT COUNT(DISTINCT batch_id) FROM history", -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int db_get_batches_paged(DbHandle db, int offset, int limit, BatchInfo *list)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, n = 0;
    if (!h || !list || limit <= 0) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT DISTINCT batch_id, save_time FROM history "
        "ORDER BY batch_id DESC LIMIT ? OFFSET ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);
    while (n < limit && sqlite3_step(stmt) == SQLITE_ROW) {
        list[n].batch_id = sqlite3_column_int(stmt, 0);
        strncpy(list[n].save_time, (const char *)sqlite3_column_text(stmt, 1), DB_MAX_TIME - 1);
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

/* --- machining-time / product statistics sampling tables --- */

static void bind_machine_sample(sqlite3_stmt *stmt, const MachineSample *s)
{
    sqlite3_bind_int(stmt, 1, s->machine_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)s->ts);
    sqlite3_bind_int(stmt, 3, s->run);
    sqlite3_bind_int(stmt, 4, s->aut);
    sqlite3_bind_int(stmt, 5, s->tmmode);
    sqlite3_bind_int(stmt, 6, s->program_no);
    sqlite3_bind_text(stmt, 7, s->program_comment, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 8, (sqlite3_int64)s->part_total);
    sqlite3_bind_int64(stmt, 9, (sqlite3_int64)s->part_current);
    sqlite3_bind_int64(stmt, 10, (sqlite3_int64)s->part_required);
}

static void fill_machine_sample(sqlite3_stmt *stmt, MachineSample *s)
{
    s->machine_id = sqlite3_column_int(stmt, 0);
    s->ts = (long)sqlite3_column_int64(stmt, 1);
    s->run = sqlite3_column_int(stmt, 2);
    s->aut = sqlite3_column_int(stmt, 3);
    s->tmmode = sqlite3_column_int(stmt, 4);
    s->program_no = sqlite3_column_int(stmt, 5);
    strncpy(s->program_comment, (const char *)sqlite3_column_text(stmt, 6), DB_MAX_COMMENT - 1);
    s->program_comment[DB_MAX_COMMENT - 1] = '\0';
    s->part_total = (long)sqlite3_column_int64(stmt, 7);
    s->part_current = (long)sqlite3_column_int64(stmt, 8);
    s->part_required = (long)sqlite3_column_int64(stmt, 9);
}

int db_add_sample(DbHandle db, const MachineSample *s)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h || !s) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "INSERT INTO machine_samples (machine_id, ts, run, aut, tmmode, program_no, "
        "program_comment, part_total, part_current, part_required) "
        "VALUES (?,?,?,?,?,?,?,?,?,?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    bind_machine_sample(stmt, s);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_add_sample_bulk(DbHandle db, const MachineSample *s, int count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, i;
    if (!h || !s || count <= 0) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "INSERT INTO machine_samples (machine_id, ts, run, aut, tmmode, program_no, "
        "program_comment, part_total, part_current, part_required) "
        "VALUES (?,?,?,?,?,?,?,?,?,?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_exec(h->db, "BEGIN", NULL, NULL, NULL);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return -1; }
    for (i = 0; i < count; i++) {
        bind_machine_sample(stmt, &s[i]);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_exec(h->db, "ROLLBACK", NULL, NULL, NULL);
            sqlite3_finalize(stmt);
            return -1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_exec(h->db, "COMMIT", NULL, NULL, NULL);
    sqlite3_finalize(stmt);
    return 0;
}

int db_upsert_machine_latest(DbHandle db, const MachineLatest *l)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h || !l) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "INSERT INTO machine_latest (machine_id, ts, run, aut, tmmode, program_no, "
        "program_comment, part_total, part_current, part_required) "
        "VALUES (?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(machine_id) DO UPDATE SET "
        "ts=excluded.ts, run=excluded.run, aut=excluded.aut, tmmode=excluded.tmmode, "
        "program_no=excluded.program_no, program_comment=excluded.program_comment, "
        "part_total=excluded.part_total, part_current=excluded.part_current, "
        "part_required=excluded.part_required",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, l->machine_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)l->ts);
    sqlite3_bind_int(stmt, 3, l->run);
    sqlite3_bind_int(stmt, 4, l->aut);
    sqlite3_bind_int(stmt, 5, l->tmmode);
    sqlite3_bind_int(stmt, 6, l->program_no);
    sqlite3_bind_text(stmt, 7, l->program_comment, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 8, (sqlite3_int64)l->part_total);
    sqlite3_bind_int64(stmt, 9, (sqlite3_int64)l->part_current);
    sqlite3_bind_int64(stmt, 10, (sqlite3_int64)l->part_required);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_samples_count(DbHandle db, int machine_id, long t0, long t1)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, count = 0;
    if (!h) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT COUNT(*) FROM machine_samples "
        "WHERE machine_id=? AND ts>=? AND ts<=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, machine_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)t0);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)t1);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

int db_get_samples(DbHandle db, int machine_id, long t0, long t1,
                   MachineSample *list, int max_count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, n = 0;
    if (!h || !list || max_count <= 0) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT machine_id, ts, run, aut, tmmode, program_no, program_comment, "
        "part_total, part_current, part_required FROM machine_samples "
        "WHERE machine_id=? AND ts>=? AND ts<=? ORDER BY ts ASC LIMIT ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, machine_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)t0);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)t1);
    sqlite3_bind_int(stmt, 4, max_count);
    while (n < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        fill_machine_sample(stmt, &list[n]);
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

int db_get_buckets(DbHandle db, int machine_id, long t0, long t1,
                   MachineBucket *list, int max_count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, n = 0;
    if (!h || !list || max_count <= 0) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT (ts / 1800) * 1800, MIN(ts), MAX(ts), "
        "  SUM(CASE WHEN run=3 AND aut=1 AND tmmode=0 THEN 1 ELSE 0 END), "
        "  COUNT(*), MIN(part_total), MAX(part_total) "
        "FROM machine_samples "
        "WHERE machine_id=? AND ts>=? AND ts<=? AND part_total>=0 "
        "GROUP BY (ts / 1800) ORDER BY (ts / 1800) ASC LIMIT ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, machine_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)t0);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)t1);
    sqlite3_bind_int(stmt, 4, max_count);
    while (n < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        list[n].ts_bucket = (long)sqlite3_column_int64(stmt, 0);
        list[n].ts_begin = (long)sqlite3_column_int64(stmt, 1);
        list[n].ts_end = (long)sqlite3_column_int64(stmt, 2);
        list[n].machining_count = (int)sqlite3_column_int64(stmt, 3);
        list[n].sample_count = sqlite3_column_int(stmt, 4);
        list[n].part_begin = (long)sqlite3_column_int64(stmt, 5);
        list[n].part_end = (long)sqlite3_column_int64(stmt, 6);
        list[n].produced = (list[n].part_end - list[n].part_begin >= 0)
            ? (list[n].part_end - list[n].part_begin) : 0;
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

int db_get_products(DbHandle db, int machine_id, long t0, long t1,
                    ProductGroup *list, int max_count)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc, n = 0;
    if (!h || !list || max_count <= 0) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT program_comment, "
        "  SUM(CASE WHEN run=3 AND aut=1 AND tmmode=0 THEN 1 ELSE 0 END), "
        "  MAX(part_total) - MIN(part_total), COUNT(*) "
        "FROM machine_samples "
        "WHERE machine_id=? AND ts>=? AND ts<=? AND part_total>=0 "
        "GROUP BY program_comment ORDER BY SUM(run) DESC LIMIT ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, machine_id);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)t0);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)t1);
    sqlite3_bind_int(stmt, 4, max_count);
    while (n < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        list[n].machine_id = machine_id;
        strncpy(list[n].program_comment,
                (const char *)sqlite3_column_text(stmt, 0), DB_MAX_COMMENT - 1);
        list[n].program_comment[DB_MAX_COMMENT - 1] = '\0';
        list[n].machining_count = (int)sqlite3_column_int64(stmt, 1);
        list[n].produced = (long)sqlite3_column_int64(stmt, 2);
        if (list[n].produced < 0) list[n].produced = 0;
        list[n].rows = (long)sqlite3_column_int64(stmt, 3);
        n++;
    }
    sqlite3_finalize(stmt);
    return n;
}

int db_set_pruned_before(DbHandle db, long ts)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "INSERT OR REPLACE INTO _meta (key, value) VALUES ('pruned_before', ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)ts);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_pruned_before(DbHandle db)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    long ts = 0;
    if (!h) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT value FROM _meta WHERE key='pruned_before'", -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW)
        ts = (long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return (int)ts;
}

int db_set_meta_int(DbHandle db, const char *key, long value)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h || !key) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "INSERT OR REPLACE INTO _meta (key, value) VALUES (?, ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)value);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

long db_get_meta_int(DbHandle db, const char *key)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    long value = -1;
    if (!h || !key) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT value FROM _meta WHERE key=?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = (long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

int db_get_sample_time_range(DbHandle db, int machine_id, long *tmin, long *tmax)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    if (!h) return -1;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT MIN(ts), MAX(ts) FROM machine_samples WHERE machine_id=?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, machine_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            *tmin = (long)sqlite3_column_int64(stmt, 0);
            *tmax = (long)sqlite3_column_int64(stmt, 1);
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return -1;
}

long db_count_samples_older_than(DbHandle db, long cutoff)
{
    struct DbHandle *h = cast_handle(db);
    sqlite3_stmt *stmt;
    int rc;
    long n = 0;
    if (!h) return 0;
    rc = sqlite3_prepare_v2(h->db,
        "SELECT COUNT(*) FROM machine_samples WHERE ts<?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        n = (long)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return n;
}

long db_archive_samples_older_than(DbHandle db, const char *archive_path, long cutoff)
{
    struct DbHandle *h = cast_handle(db);
    char *attach;
    char *errmsg = NULL;
    long n = 0;
    int rc;

    if (!h || !archive_path) return -1;

    attach = sqlite3_mprintf("ATTACH DATABASE '%q' AS archive;", archive_path);
    if (!attach) return -1;
    rc = sqlite3_exec(h->db, attach, NULL, NULL, &errmsg);
    sqlite3_free(attach);
    if (rc != SQLITE_OK) { if (errmsg) sqlite3_free(errmsg); return -1; }

    rc = sqlite3_exec(h->db,
        "CREATE TABLE IF NOT EXISTS archive.machine_samples ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  machine_id INTEGER NOT NULL, ts INTEGER NOT NULL,"
        "  run INTEGER NOT NULL DEFAULT 0, aut INTEGER NOT NULL DEFAULT 0,"
        "  tmmode INTEGER NOT NULL DEFAULT 0, program_no INTEGER NOT NULL DEFAULT 0,"
        "  program_comment TEXT NOT NULL DEFAULT '',"
        "  part_total INTEGER NOT NULL DEFAULT 0,"
        "  part_current INTEGER NOT NULL DEFAULT 0,"
        "  part_required INTEGER NOT NULL DEFAULT 0"
        ");",
        NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) { if (errmsg) sqlite3_free(errmsg); sqlite3_exec(h->db, "DETACH DATABASE archive", NULL, NULL, NULL); return -1; }
    rc = sqlite3_exec(h->db,
        "CREATE INDEX IF NOT EXISTS idx_samples_mach_ts "
        "  ON machine_samples(machine_id, ts);",
        NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) { if (errmsg) sqlite3_free(errmsg); sqlite3_exec(h->db, "DETACH DATABASE archive", NULL, NULL, NULL); return -1; }

    /* transaction: copy then delete */
    rc = sqlite3_exec(h->db, "BEGIN", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) { if (errmsg) sqlite3_free(errmsg); sqlite3_exec(h->db, "DETACH DATABASE archive", NULL, NULL, NULL); return -1; }

    attach = sqlite3_mprintf(
        "INSERT INTO archive.machine_samples "
        "  (machine_id, ts, run, aut, tmmode, program_no, program_comment, "
        "   part_total, part_current, part_required) "
        "SELECT machine_id, ts, run, aut, tmmode, program_no, program_comment, "
        "       part_total, part_current, part_required "
        "FROM main.machine_samples WHERE ts<%lld",
        (long long)cutoff);
    rc = sqlite3_exec(h->db, attach, NULL, NULL, &errmsg);
    sqlite3_free(attach);
    if (rc != SQLITE_OK) { if (errmsg) sqlite3_free(errmsg); sqlite3_exec(h->db, "ROLLBACK", NULL, NULL, NULL); sqlite3_exec(h->db, "DETACH DATABASE archive", NULL, NULL, NULL); return -1; }

    n = db_count_samples_older_than(db, cutoff);

    attach = sqlite3_mprintf("DELETE FROM main.machine_samples WHERE ts<%lld",
                             (long long)cutoff);
    rc = sqlite3_exec(h->db, attach, NULL, NULL, &errmsg);
    sqlite3_free(attach);
    if (rc != SQLITE_OK) { if (errmsg) sqlite3_free(errmsg); sqlite3_exec(h->db, "ROLLBACK", NULL, NULL, NULL); sqlite3_exec(h->db, "DETACH DATABASE archive", NULL, NULL, NULL); return -1; }

    rc = sqlite3_exec(h->db, "COMMIT", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) { if (errmsg) sqlite3_free(errmsg); sqlite3_exec(h->db, "ROLLBACK", NULL, NULL, NULL); sqlite3_exec(h->db, "DETACH DATABASE archive", NULL, NULL, NULL); return -1; }

    sqlite3_exec(h->db, "DETACH DATABASE archive", NULL, NULL, NULL);
    return n;
}
