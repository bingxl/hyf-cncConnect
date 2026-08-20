/*
 * db_mysql.cpp - MySQL / MariaDB 后端（数据库抽象层）
 *
 * 编译：构建脚本检测到 MySQL/MariaDB 客户端（MTC_MYSQL_ROOT）时定义
 * MTC_HAVE_MYSQL 并链接 libmariadb/libmysql；未找到时编译为 stub，
 * 运行时报 "MySQL/MariaDB client library not available"。
 *
 * Statement 采用"拼接 SQL + 参数转义"实现（本层使用量小、SQL 固定），
 * 避免手写 MYSQL_BIND 输出绑定；与 SQLite 后端的接口行为一致。
 */
#include "db/db.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#ifdef MTC_HAVE_MYSQL
#if defined(__has_include)
#if __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#else
#include <mysql.h>
#endif
#else
#include <mysql.h>
#endif
#endif

namespace db {

#ifdef MTC_HAVE_MYSQL
namespace {

static std::string mysql_escape(MYSQL *m, const std::string &s)
{
    if (s.empty()) return "''";
    std::string out(s.size() * 2 + 3, '\0');
    unsigned long n = mysql_real_escape_string(m, &out[0], s.data(),
                                               (unsigned long)s.size());
    out.resize(n);
    return "'" + out + "'";
}

class MysqlStatement : public Statement {
public:
    MysqlStatement(MYSQL *conn, const std::string &sql)
        : conn_(conn), sql_(sql) {}

    ~MysqlStatement() override
    {
        if (res_) mysql_free_result(res_);
    }

    bool step() override
    {
        if (res_) { mysql_free_result(res_); res_ = nullptr; row_ = nullptr; }
        std::string q = build();
        if (mysql_real_query(conn_, q.data(), (unsigned long)q.size()) != 0) {
            ok_ = false;
            return false;
        }
        res_ = mysql_store_result(conn_);
        if (res_) {
            row_ = mysql_fetch_row(res_);
            if (row_) {
                fields_ = mysql_num_fields(res_);
                return true;
            }
            mysql_free_result(res_);
            res_ = nullptr;
            return false;
        }
        ok_ = true; /* 无结果集（INSERT/UPDATE/DELETE/DDL） */
        return false;
    }

    bool ok() const override { return ok_; }

    void bind_int64(int idx, long long v) override { params_[idx] = std::to_string(v); }
    void bind_int(int idx, int v) override { params_[idx] = std::to_string(v); }
    void bind_text(int idx, const std::string &v) override
    {
        params_[idx] = mysql_escape(conn_, v);
    }

    void reset() override
    {
        params_.clear();
        ok_ = true;
    }

    long long column_int64(int idx) const override
    {
        return (row_ && idx < (int)fields_ && row_[idx]) ? _atoi64(row_[idx]) : 0;
    }
    int column_int(int idx) const override { return (int)column_int64(idx); }
    std::string column_text(int idx) const override
    {
        return (row_ && idx < (int)fields_ && row_[idx]) ? row_[idx] : "";
    }
    bool column_is_null(int idx) const override
    {
        return !row_ || idx >= (int)fields_ || row_[idx] == nullptr;
    }

    long long affected_rows() override
    {
        return conn_ ? (long long)mysql_affected_rows(conn_) : 0;
    }

private:
    /* 把 ?N 占位符替换为已转义参数（从大到小替换，避免 ?1 命中 ?10 之类） */
    std::string build()
    {
        std::string q = sql_;
        for (int i = 63; i >= 1; i--) {
            char ph[16];
            snprintf(ph, sizeof(ph), "?%d", i);
            auto it = params_.find(i);
            std::string repl = it != params_.end() ? it->second : "NULL";
            size_t pos = 0;
            while ((pos = q.find(ph, pos)) != std::string::npos) {
                q.replace(pos, strlen(ph), repl);
                pos += repl.size();
            }
        }
        return q;
    }

    MYSQL *conn_ = nullptr;
    std::string sql_;
    std::map<int, std::string> params_;
    MYSQL_RES *res_ = nullptr;
    MYSQL_ROW row_ = nullptr;
    unsigned int fields_ = 0;
    bool ok_ = true;
};

class MysqlDatabase : public Database {
public:
    explicit MysqlDatabase(MYSQL *conn) : conn_(conn) {}
    ~MysqlDatabase() override
    {
        if (conn_) mysql_close(conn_);
    }

    Backend backend() const override { return Backend::Mysql; }

    bool exec(const std::string &sql) override
    {
        if (mysql_real_query(conn_, sql.data(), (unsigned long)sql.size()) != 0) {
            err_ = mysql_error(conn_);
            return false;
        }
        /* 消耗多语句结果集（多条 DDL/DML 以 ; 分隔） */
        do {
            MYSQL_RES *r = mysql_store_result(conn_);
            if (r) mysql_free_result(r);
        } while (mysql_next_result(conn_) == 0);
        return true;
    }

    std::unique_ptr<Statement> prepare(const std::string &sql) override
    {
        return std::make_unique<MysqlStatement>(conn_, sql);
    }

    bool begin() override { return exec("START TRANSACTION;"); }
    bool commit() override { return exec("COMMIT;"); }
    bool rollback() override { return exec("ROLLBACK;"); }
    std::string last_error() const override { return err_; }

private:
    MYSQL *conn_ = nullptr;
    std::string err_;
};

} // namespace

std::unique_ptr<Database> open_mysql(const Config &cfg, std::string *err)
{
    if (cfg.database.empty()) {
        if (err) *err = "mysql backend requires db.database (config.json)";
        return nullptr;
    }
    MYSQL *c = mysql_init(nullptr);
    if (!c) {
        if (err) *err = "mysql_init failed";
        return nullptr;
    }
    const char *host = cfg.host.empty() ? "127.0.0.1" : cfg.host.c_str();
    unsigned int port = cfg.port > 0 ? (unsigned int)cfg.port : 3306;
    if (!mysql_real_connect(c, host, cfg.user.c_str(), cfg.password.c_str(),
                            cfg.database.c_str(), port, nullptr,
                            CLIENT_MULTI_STATEMENTS)) {
        if (err) *err = mysql_error(c);
        mysql_close(c);
        return nullptr;
    }
    mysql_set_character_set(c, "utf8mb4");
    return std::make_unique<MysqlDatabase>(c);
}

#else /* !MTC_HAVE_MYSQL */

std::unique_ptr<Database> open_mysql(const Config &cfg, std::string *err)
{
    (void)cfg;
    if (err)
        *err = "MySQL/MariaDB client library not available; set MTC_MYSQL_ROOT "
               "and rebuild";
    return nullptr;
}

#endif /* MTC_HAVE_MYSQL */

} // namespace db
