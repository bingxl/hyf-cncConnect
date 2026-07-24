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
        ");";
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
