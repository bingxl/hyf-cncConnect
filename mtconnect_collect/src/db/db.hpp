/*
 * db.hpp - 数据库抽象层
 *
 * 统一 mtc_stats / webserver 的数据库访问。当前实现 SQLite 后端；
 * 接口按 SQL/MySQL 可迁移设计：SQL 差异集中在本层（upsert 等），
 * 业务代码只依赖 Database / Statement 两个抽象。
 *
 * 迁移到 MySQL 时：新增 db_mysql.cpp 实现 Database/Statement，
 * 并在 db::open() 的 factory 中按 Config::backend 分发即可；
 * Config 已预留 host/port/user/password/database 字段。
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace db {

enum class Backend {
    Sqlite,
    Mysql,   /* 预留：尚未实现 */
    Postgres /* 预留：尚未实现 */
};

struct Config {
    Backend backend = Backend::Sqlite;
    std::string file = "stats.db"; /* SQLite 文件路径 */
    /* 预留：MySQL/PostgreSQL 连接参数 */
    std::string host;
    int port = 0;
    std::string user;
    std::string password;
    std::string database;
};

/* 一次预编译查询：绑定参数 -> step() 逐行读取 -> 读取列 */
class Statement {
public:
    virtual ~Statement() = default;

    /* 执行一步；返回 true 表示有一行可读，false 表示结束（done/error） */
    virtual bool step() = 0;
    /* 最后一次 step() 是否成功（有行或正常结束） */
    virtual bool ok() const = 0;

    virtual void bind_int64(int idx, long long v) = 0;
    virtual void bind_int(int idx, int v) = 0;
    virtual void bind_text(int idx, const std::string &v) = 0;
    /* 复用语句：清空绑定，准备下一次执行 */
    virtual void reset() = 0;

    virtual long long column_int64(int idx) const = 0;
    virtual int column_int(int idx) const = 0;
    virtual std::string column_text(int idx) const = 0;
    virtual bool column_is_null(int idx) const = 0;

    /* 上一条写语句影响的行数（如 DELETE/UPDATE），无操作为 0 */
    virtual long long affected_rows() = 0;
};

class Database {
public:
    virtual ~Database() = default;

    /* 执行多条/单条 SQL（不返回结果集） */
    virtual bool exec(const std::string &sql) = 0;
    /* 预编译；失败返回 nullptr（last_error 可查原因） */
    virtual std::unique_ptr<Statement> prepare(const std::string &sql) = 0;

    virtual bool begin() = 0;
    virtual bool commit() = 0;
    virtual bool rollback() = 0;

    virtual std::string last_error() const = 0;

    /* 当前后端类型（stats_db 等共享代码据此选择方言 SQL） */
    virtual Backend backend() const = 0;
};

/* 打开数据库（按 Config::backend 分发）；失败返回 nullptr 并填充 err */
std::unique_ptr<Database> open(const Config &cfg, std::string *err);

/* MySQL/MariaDB 后端（db_mysql.cpp；未编译客户端库时为 stub） */
std::unique_ptr<Database> open_mysql(const Config &cfg, std::string *err);

const char *backend_name(Backend b);

} // namespace db
