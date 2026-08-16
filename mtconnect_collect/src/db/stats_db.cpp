/* stats_db.cpp - 统计库共享访问（SQL 后端差异集中于此） */
#include "db/stats_db.hpp"

namespace db {

static const char *kStatsSchema =
    "CREATE TABLE IF NOT EXISTS samples("
    "  ts INTEGER NOT NULL,"
    "  machine VARCHAR(64) NOT NULL,"
    "  execution TEXT, mode TEXT, tmmode TEXT,"
    "  program TEXT, comment TEXT,"
    "  part_total INTEGER, power INTEGER,"
    "  PRIMARY KEY(ts, machine));"
    "CREATE INDEX IF NOT EXISTS idx_samples_machine ON samples(machine, ts);"
    "CREATE TABLE IF NOT EXISTS stream_state("
    "  key VARCHAR(64) PRIMARY KEY, value TEXT);"
    "CREATE TABLE IF NOT EXISTS alarms("
    "  machine VARCHAR(64) NOT NULL,"
    "  item_id VARCHAR(128) NOT NULL,"
    "  item_type VARCHAR(64),"
    "  state TEXT,"
    "  first_ts INTEGER NOT NULL,"
    "  last_ts INTEGER NOT NULL,"
    "  end_ts INTEGER,"
    "  active INTEGER NOT NULL DEFAULT 1,"
    "  PRIMARY KEY(machine, item_id, first_ts));"
    "CREATE INDEX IF NOT EXISTS idx_alarms_machine ON alarms(machine, last_ts);";

bool ensure_stats_schema(Database &d, std::string *err)
{
    if (!d.exec(kStatsSchema)) {
        if (err) *err = d.last_error();
        return false;
    }
    /* 旧库迁移：samples 缺 power 列时补列（探测失败 = 列不存在） */
    auto probe = d.prepare("SELECT power FROM samples LIMIT 1;");
    if (!probe) {
        if (!d.exec("ALTER TABLE samples ADD COLUMN power INTEGER;")) {
            if (err) *err = d.last_error();
            return false;
        }
    }
    return true;
}

std::unique_ptr<Statement> prepare_sample_upsert(Database &d, std::string *err)
{
    const char *sql = nullptr;
    switch (d.backend()) {
    case Backend::Sqlite:
        sql = "INSERT OR REPLACE INTO samples(ts,machine,execution,mode,tmmode,"
              "program,comment,part_total,power) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9);";
        break;
    case Backend::Mysql:
        /* 预留：MySQL 8 upsert 语法 */
        sql = "INSERT INTO samples(ts,machine,execution,mode,tmmode,program,comment,"
              "part_total,power) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) "
              "ON DUPLICATE KEY UPDATE execution=VALUES(execution), mode=VALUES(mode),"
              " tmmode=VALUES(tmmode), program=VALUES(program), comment=VALUES(comment),"
              " part_total=VALUES(part_total), power=VALUES(power);";
        break;
    default:
        if (err) *err = "upsert not implemented for backend";
        return nullptr;
    }
    auto st = d.prepare(sql);
    if (!st && err) *err = d.last_error();
    return st;
}

std::string get_state(Database &d, const std::string &key)
{
    auto st = d.prepare("SELECT value FROM stream_state WHERE key=?1;");
    if (!st) return "";
    st->bind_text(1, key);
    if (!st->step()) return "";
    return st->column_text(0);
}

void set_state(Database &d, const std::string &key, const std::string &value)
{
    std::unique_ptr<Statement> st;
    if (d.backend() == Backend::Mysql)
        st = d.prepare("INSERT INTO stream_state(key,value) VALUES(?1,?2) "
                       "ON DUPLICATE KEY UPDATE value=VALUES(value);");
    else
        st = d.prepare("INSERT OR REPLACE INTO stream_state(key,value) VALUES(?1,?2);");
    if (!st) return;
    st->bind_text(1, key);
    st->bind_text(2, value);
    st->step();
}

long long prune_samples(Database &d, long long cutoff, std::string *err)
{
    auto st = d.prepare("DELETE FROM samples WHERE ts < ?1;");
    if (!st) {
        if (err) *err = d.last_error();
        return -1;
    }
    st->bind_int64(1, cutoff);
    st->step();
    if (!st->ok()) {
        if (err) *err = d.last_error();
        return -1;
    }
    return st->affected_rows();
}

bool alarm_upsert(Database &d, const std::string &machine,
                  const std::string &item_id, const std::string &item_type,
                  const std::string &state, long long now)
{
    {
        auto st = d.prepare(
            "UPDATE alarms SET last_ts=?1, state=?2, item_type=?3 "
            "WHERE machine=?4 AND item_id=?5 AND active=1;");
        if (!st) return false;
        st->bind_int64(1, now);
        st->bind_text(2, state);
        st->bind_text(3, item_type);
        st->bind_text(4, machine);
        st->bind_text(5, item_id);
        st->step();
        if (st->ok() && st->affected_rows() > 0) return true;
    }
    auto st = d.prepare(
        "INSERT INTO alarms(machine,item_id,item_type,state,first_ts,last_ts,active) "
        "VALUES(?1,?2,?3,?4,?5,?5,1);");
    if (!st) return false;
    st->bind_text(1, machine);
    st->bind_text(2, item_id);
    st->bind_text(3, item_type);
    st->bind_text(4, state);
    st->bind_int64(5, now);
    st->step();
    return st->ok();
}

bool alarm_clear(Database &d, const std::string &machine,
                 const std::string &item_id, long long now)
{
    auto st = d.prepare(
        "UPDATE alarms SET active=0, end_ts=?1, last_ts=?1 "
        "WHERE machine=?2 AND item_id=?3 AND active=1;");
    if (!st) return false;
    st->bind_int64(1, now);
    st->bind_text(2, machine);
    st->bind_text(3, item_id);
    st->step();
    return st->ok();
}

long long prune_alarms(Database &d, long long cutoff, std::string *err)
{
    auto st = d.prepare(
        "DELETE FROM alarms WHERE active=0 AND end_ts IS NOT NULL AND end_ts < ?1;");
    if (!st) {
        if (err) *err = d.last_error();
        return -1;
    }
    st->bind_int64(1, cutoff);
    st->step();
    if (!st->ok()) {
        if (err) *err = d.last_error();
        return -1;
    }
    return st->affected_rows();
}

} // namespace db
