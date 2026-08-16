/*
 * stats_db.hpp - 统计库共享访问（基于 db 抽象层）
 *
 * samples / stream_state 建表、采样行 upsert、stream 状态、保留清理。
 * 后端差异（如 upsert 语法）集中在这里，迁移 MySQL 时只改本文件 + 新增后端。
 */
#pragma once

#include <memory>
#include <string>

#include "db/db.hpp"

namespace db {

/* 建表 + 旧库迁移（缺 power 列时补列）；失败返回 false 并填充 err */
bool ensure_stats_schema(Database &d, std::string *err);

/* 采样行 upsert 语句（9 个参数：ts,machine,execution,mode,tmmode,
   program,comment,part_total,power）；按后端生成 SQL */
std::unique_ptr<Statement> prepare_sample_upsert(Database &d, std::string *err);

/* stream_state 读写 */
std::string get_state(Database &d, const std::string &key);
void set_state(Database &d, const std::string &key, const std::string &value);

/* 保留策略：删除 ts < cutoff 的采样；返回删除行数，失败返回 -1 */
long long prune_samples(Database &d, long long cutoff, std::string *err);

/* ---- 报警（conditions / estop）---- */

/* 报警行写入：已存在 active 行则刷新 last_ts/state，否则新开一条 occurrence。
   每条 occurrence 以 (machine,item_id,first_ts) 唯一，便于保留完整历史。 */
bool alarm_upsert(Database &d, const std::string &machine,
                  const std::string &item_id, const std::string &item_type,
                  const std::string &state, long long now);

/* 关闭指定 active 报警（记录 end_ts） */
bool alarm_clear(Database &d, const std::string &machine,
                 const std::string &item_id, long long now);

/* 删除已关闭且 end_ts < cutoff 的历史报警；返回删除行数，失败返回 -1 */
long long prune_alarms(Database &d, long long cutoff, std::string *err);

} // namespace db
