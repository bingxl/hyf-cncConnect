/*
 * config.hpp - 统一配置文件（config.json）
 *
 * 所有可配置项（端口、路径、数据库、采集间隔/超时、保留策略、告警、
 * 适配器参数、日志目录等）统一放在 config.json 中：
 *   查找顺序：<项目根>/config.json -> ./config.json -> %USERPROFILE%\mtconnect\config.json
 *   找不到时使用内置默认值，并把默认配置写入 config.json（优先写项目根）。
 *
 * 各程序（mtc_ctl / mtc_stats / webserver / fanuc_adapter / mazak_adapter）
 * 启动时调用 cfg::load() 填充默认值，命令行参数仍可覆盖。
 */
#pragma once

#include <cstddef>
#include <string>

namespace cfg {

struct Config {
    /* 项目根目录 / 配置来源 */
    std::string root;        /* 项目根（mtc_ctl 使用，可由 --root 覆盖） */
    std::string config_path; /* 实际加载的配置文件路径（空 = 内置默认） */

    /* agent 相关（genconfig / mtc_ctl） */
    int agent_http_port = 5000;
    int shdr_base_port = 7878;
    std::string agent_dir = "agent";
    std::string devices_dir = "devices";
    bool monitor_config_files = false;
    int agent_buffer_size = 17;

    /* 数据库 */
    std::string db_type = "sqlite"; /* sqlite | mysql(预留) | postgres(预留) */
    std::string db_path = "stats.db";
    std::string db_host;
    int db_port = 0;
    std::string db_user;
    std::string db_password;
    std::string db_database;

    /* 统计采集（mtc_stats / mtc_ctl） */
    int stream_interval_ms = 5000;
    int poll_interval_sec = 5;
    int sample_count = 1000;
    int receive_timeout_ms = 15000;
    long long power_gap_max = 90; /* 相邻样本间隔超过视为断联（秒） */
    int retention_days = 90;
    long long prune_interval_sec = 3600;
    std::string alert_url;
    int alert_min = 60;

    /* web（webserver） */
    int web_port = 8088;
    std::string web_root = "web/dist";
    int web_agent_port = 5000; /* /api/live/current 转发 agent 的端口 */
    std::size_t web_max_rows = 1000000;
    int web_default_bucket_sec = 1800;

    /* FANUC 适配器 */
    int fanuc_connect_timeout_sec = 10;
    int fanuc_reconnect_wait_ms = 5000;
    int fanuc_scan_delay_ms = 100;
    int fanuc_comment_retry_ms = 5000;

    /* Mazak 适配器 */
    int mazak_scan_delay_ms = 100;
    int mazak_socket_timeout_ms = 3000;

    /* 日志 */
    std::string log_dir = "log";
};

/* 加载配置：搜索 root/config.json -> ./config.json -> %USERPROFILE%\mtconnect\config.json；
   找不到时写入默认配置（优先 root，其次 cwd）。root 为空时跳过 root 路径。 */
bool load(Config &c, const std::string &rootHint, std::string *err);

/* 把默认配置写入指定文件 */
bool save_default(const std::string &path, std::string *err);

} // namespace cfg
