/* db.cpp - SQLite 后端实现（数据库抽象层） */
#include "db/db.hpp"

#include <sqlite3.h>

namespace db {
namespace {

class SqliteStatement : public Statement {
public:
    SqliteStatement(sqlite3 *conn, sqlite3_stmt *st)
        : conn_(conn), st_(st), rc_(SQLITE_DONE) {}

    ~SqliteStatement() override
    {
        if (st_) sqlite3_finalize(st_);
    }

    bool step() override
    {
        rc_ = sqlite3_step(st_);
        return rc_ == SQLITE_ROW;
    }
    bool ok() const override { return rc_ == SQLITE_ROW || rc_ == SQLITE_DONE; }

    void bind_int64(int idx, long long v) override { sqlite3_bind_int64(st_, idx, v); }
    void bind_int(int idx, int v) override { sqlite3_bind_int(st_, idx, v); }
    void bind_text(int idx, const std::string &v) override
    {
        sqlite3_bind_text(st_, idx, v.c_str(), -1, SQLITE_TRANSIENT);
    }
    void reset() override
    {
        sqlite3_reset(st_);
        sqlite3_clear_bindings(st_);
        rc_ = SQLITE_DONE;
    }

    long long column_int64(int idx) const override { return sqlite3_column_int64(st_, idx); }
    int column_int(int idx) const override { return sqlite3_column_int(st_, idx); }
    std::string column_text(int idx) const override
    {
        const unsigned char *p = sqlite3_column_text(st_, idx);
        return p ? reinterpret_cast<const char *>(p) : "";
    }
    bool column_is_null(int idx) const override
    {
        return sqlite3_column_type(st_, idx) == SQLITE_NULL;
    }

    long long affected_rows() override { return sqlite3_changes(conn_); }

private:
    sqlite3 *conn_ = nullptr;
    sqlite3_stmt *st_ = nullptr;
    int rc_ = SQLITE_DONE;
};

class SqliteDatabase : public Database {
public:
    explicit SqliteDatabase(sqlite3 *conn) : conn_(conn) {}
    ~SqliteDatabase() override
    {
        if (conn_) sqlite3_close(conn_);
    }

    Backend backend() const override { return Backend::Sqlite; }

    bool exec(const std::string &sql) override
    {
        char *err = nullptr;
        if (sqlite3_exec(conn_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            err_ = err ? err : "sqlite error";
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    std::unique_ptr<Statement> prepare(const std::string &sql) override
    {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(conn_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
            err_ = sqlite3_errmsg(conn_);
            return nullptr;
        }
        return std::make_unique<SqliteStatement>(conn_, st);
    }

    bool begin() override { return exec("BEGIN;"); }
    bool commit() override { return exec("COMMIT;"); }
    bool rollback() override { return exec("ROLLBACK;"); }
    std::string last_error() const override { return err_; }

private:
    sqlite3 *conn_ = nullptr;
    std::string err_;
};

} // namespace

std::unique_ptr<Database> open(const Config &cfg, std::string *err)
{
    switch (cfg.backend) {
    case Backend::Sqlite: {
        sqlite3 *conn = nullptr;
        if (sqlite3_open(cfg.file.c_str(), &conn) != SQLITE_OK) {
            if (err) *err = conn ? sqlite3_errmsg(conn) : "sqlite open failed";
            if (conn) sqlite3_close(conn);
            return nullptr;
        }
        return std::make_unique<SqliteDatabase>(conn);
    }
    case Backend::Mysql:
        return open_mysql(cfg, err);
    case Backend::Postgres:
        if (err) *err = "postgres backend not built yet";
        return nullptr;
    }
    if (err) *err = "unknown backend";
    return nullptr;
}

const char *backend_name(Backend b)
{
    switch (b) {
    case Backend::Sqlite: return "sqlite";
    case Backend::Mysql: return "mysql";
    case Backend::Postgres: return "postgres";
    }
    return "?";
}

} // namespace db
