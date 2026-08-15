# 任务上下文：机床加工时间与按产品统计

> 保存时间：2026-08-13
> 项目：F:\code\cnc_test（C/FOCAS2 + ImGui GUI + SQLite）
> 目的：本文件用于任务中断/重启后快速恢复，包含需求、已确认决策、实现规划与文件约定。

---

## 一、需求

### 需求 1
指定时间段内，每台机器的**实际加工时间**以及**加工件数**。

### 需求 2
通过程序的 **comment 获取加工产品编号**，在需求 1 的前提下，给出**每种产品**（按 comment 区分）的加工时间与数量。

---

## 二、已确认决策（用户拍板）

| # | 决策项 | 结论 |
|---|--------|------|
| 1 | 实际加工时间定义 | **`run==3`(STaRT) 且 `aut==1`(MEM 内存自动) 且 `tmmode==0`(T/车床模式)**。注意：FANUC ODBST.run 是枚举值（0=reset/1=STOP/2=HOLD/3=STaRT/4=MSTR），实测运行中的机床 `run==3`——旧理解 `run==1` 会把实际加工全部漏掉，已按权威定义纠正 |
| 2 | 采样运行方式 | A：独立常驻采集程序 **`cnc_sampler.exe`**，关掉 GUI 也持续采集 |
| 3 | 采样间隔 | **可配置，默认 5s** |
| 3b | 连接方式 | 每台机床**单例模式长连接**（常驻复用），仅采集程序退出时才断开；做好**重连策略**（失败指数退避） |
| 3c | GUI 数据来源 | **GUI 需要的数据也从采集程序（其写入的数据库）获取**，GUI 不再直连 FOCAS |
| 4 | 重连策略 | 常驻复用 + 断线自动重连 |
| 5 | 时间范围选择 | **以每半小时为单位**；提供快捷键：**白班 8:30~20:30**、**夜班 20:30~次日 8:30** |
| 6 | 历史数据 | 新功能只能从开始采样后有数据，历史批次快照无法还原加工时间（用户已知晓） |
| 7 | 数据保留 | 样本保留 **90 天**，超期后**移动**到闲置数据库（单独的 db 文件） |
| 8 | 报表 | **需要** GUI 报表页（含导出） |

---

## 三、实现规划（4 大块）

### 1. 数据采集层（核心新增）

**新表 `machine_samples`**（SQLite，主库）：
```
id INTEGER PK
machine_id INTEGER            -- 关联 machines.id
ts INTEGER                    -- Unix 时间戳（秒）
run INTEGER                   -- 自动运行状态枚举（0=reset/1=STOP/2=HOLD/3=STaRT/4=MSTR）
aut INTEGER                   -- 自动/手动模式选择（0=MDI/1=MEM/…）
tmmode INTEGER                -- T/M 模式（0=T 车床 / 1=M 铣床）
program_no INTEGER            -- 程序号
program_comment TEXT          -- 程序注释 = 产品编号
part_total INTEGER            -- 累计件数（参数 #6712）
part_current INTEGER          -- 当前批次件数（宏 #3901）
part_required INTEGER         -- 要求件数（宏 #3902）
```
**判定"加工中"= `run==3 && aut==1 && tmmode==0`**（STaRT + 内存自动 + 车床模式）。

**新表 `machine_latest`**（每台机一行，GUI 详情页数据来源，采样周期内刷新）：
```
machine_id INTEGER PK
ts INTEGER
... 与 CncMachineData 对应字段（状态/程序/计件/坐标等，供 GUI 详情页）
```

**新增 DB 接口**（db_ops）：
- `db_add_sample(...)`、`db_add_machine_latest(...)`
- `db_get_samples(machine_id, t0, t1)`、`db_get_samples_products(...)`（按 comment 聚合）
- `db_archive_old_samples(days=90)`、`db_get_old_sample_range(...)`、`db_migrate_samples(archive_db_path, t0, t1)`
- 表结构版本号管理（已有 `db_get_schema_version`）

### 2. `cnc_sampler.exe`（独立常驻采样程序）

- 读取配置（`config.txt`）：机床列表、采样间隔（默认 5s）、保留天数（90）、日志级别等
- **单例长连接池**：每台机床一个 `cnc_allclibhndl3` 句柄常驻；退出时统一 `cnc_freelibhndl`
- **重连策略**：连接失败 → 指数退避（1s→2s→4s→…→30s 上限），成功后复位；每次采样前检查连接，断开则先重连
- 每周期：遍历各机（可并行）→ 读 status(run/aut/tmmode) + 程序号/comment + 计件数 → 写 `machine_samples` + 更新 `machine_latest`
- **保留策略**：每日定时把超过 90 天的样本**移动**到独立归档库（如 `archive/archive_YYYY-MM-DD.db`，跨库事务），并从主库删除
- 日志：写 `logs/sampler.log`（含连接/重连/错误/统计）
- 采样期间如果当前样本没有程序（MDI/手动/空），program_no/comment 记 0/空，归入"未识别"

### 3. 统计聚合算法

**需求 1（单机区间 [t0,t1]）**：
- 加工时间 = 相邻样本对区间累加，当 `run==3 && aut==1 && tmmode==0` 时计入 Δt
- 加工件数 = 区间内 `part_total` 差分求和；出现负差（计数器清零/复位）钳为 0

**需求 2（按产品）**：
- 将样本按 `(machine_id, program_comment)` 连续段分组
- 每段时间 = 该 comment 活跃期间满足运行+内存模式的 Δt 累加
- 每段件数 = 该 comment 活跃期间的 `part_total` 增量
- comment 空 → 归入"未识别/其他"
- 输出每产品：加工时间、数量、占比

### 4. GUI 改造 + 报表页

- **数据源切换**：Overview / MachineDetail / History 改为读 DB（`machine_latest` / `history` / `machine_samples`），不再直连 FOCAS（符合决策 3c）
- **新增「加工统计」页**：
  - 选机床 + 起止时间（每半小时粒度，提供 今天/昨天/白班/夜班 快捷键；白班 8:30~20:30，夜班 20:30~次日 8:30）
  - 表 1：每台机 总加工时间 + 总件数
  - 表 2：每台机按产品(comment) 的时间、数量、占比
  - 导出 CSV

---

## 四、文件/目录约定

所有配置、数据库、输出文件统一放用户主目录下：
```
%USERPROFILE%\data-collect\
├── config.txt                  # 采集配置（机床列表、间隔、保留天数…）
├── cnc_monitor.db              # 主数据库（machines/history/machine_samples/machine_latest）
├── archive\                    # 90 天前的归档库（独立 db 文件）
│   └── archive_YYYY-MM-DD.db
├── logs\sampler.log            # 采集程序日志
└── result.txt / *.csv          # 导出文件
```
现有 `~/.config/cnc_monitor.db` 需迁移/合并到 `data-collect\cnc_monitor.db`。

---

## 五、已完成的相关前置工作（本次会话）

- 程序注释读取已优化：`get_program_comment` 优先 `cnc_rdprogdir3` 按程序号定向读取（实测 O0801 从 ~4630ms → ~104ms），失败回退分页扫描
- 去除 `fetch_machine_data` 中重复的 `cnc_rdprogdir2` 目录读取
- 新增 `cnc_monitor.exe -bench` 基准工具（程序列表读取/注释查找耗时测量）
- 以上已提交并推送（commit `2411d75`）

---

## 六、后续待办（实现顺序）

- [x] 1. DB 层：`machine_samples`/`machine_latest` 建表 + 读写/归档接口（已完成）
- [x] 2. `cnc_sampler.exe`：长连接池、采样循环、重连退避、日志（已完成并实测）
- [x] 3. 配置：`data-collect\config.txt`（interval/retention_days；机床列表读 jichuang.txt→machines 表注册）（已完成）
- [x] 4. 聚合：半小时桶 `db_get_buckets` + 按产品 `db_get_products`（实测通过，run==3 修正）
- [x] 5. GUI：「加工统计」页（机床+时间范围+产品/桶表+导出 CSV）（已完成）
- [x] 6. 归档：超过 retention 天样本 `db_archive_samples_older_than` 移动到 archive_YYYYMMDD.db（已完成并实测）
- [x] 7. 数据目录：GUI DB 路径改为 `data-collect\cnc_monitor.db`（已完成）
- [ ] 8. 增强（低优先级/暂缓）：Overview/MachineDetail 数据源改为读 `machine_latest`（当前仍直连 FOCAS，决策 3c 的后续）
- [ ] 9. 文档更新（AGENTS.md 已更新；README 可选）

## 七、当前实现状态（2026-08-13 会话末）

已完成并验证 end-to-end：
- **cnc_sampler.exe** 采集 8 台机 → `machine_samples`/`machine_latest`（真实数据：各机 run=3/aut=1/tmmode=0，程序 comment 如 `(C-24-0000000-06332/A0)`，part_total 实时变化）
- 聚合验证：所有机床正确产出 半小时桶 与 按产品 grouping（例：ZXJ03 当日 2 桶、9 个加工样本、产品 1 种、产 5 件）
- **GUI「加工统计」页** 已注册并可启动（无崩溃），读共享 DB
- 归档：`db_archive_samples_older_than` 将 <90d 样本移动到归档 db（实测 moved=1 / main 清除）

文件/目录（%USERPROFILE%\data-collect\）：
```
config.txt / jichuang.txt / cnc_monitor.db / cnc_sampler.log / archive\archive_YYYYMMDD.db
```

遗留/注意：
- 加工判定已按权威定义修正为 `run==3`（STaRT），勿改回 run==1
- 加工时长为“加工样本数 × 采样间隔”近似（采样间隔默认 5s）
- 按产品 produced 用 `MAX(part_total)-MIN(part_total)`，跨计数器复位时会有偏差（后续可改为游标差分）
- `db_archive_samples_older_than` 用 ATTACH + 事务；注意 `sqlite3_exec` 不支持 `?` 占位符，需用 `sqlite3_mprintf` 内联值

---

## 七、注意点 / 风险

- 加工时间只能统计采样开始之后的数据；采样间隔越大精度越低（误差 ≤ 间隔/2 每处状态跳变）
- 8 台机 × 每 5s ≈ 13.8 万行/天；建议采样写库用事务批量提交（每轮一批），WAL 模式
- `part_total` 存在机床复位清零风险，差分需钳 0
- 常驻连接需处理：机床关机/断网、FOCAS 句柄失效（EW_HANDLE/-12）、超时设置
- GUI 与采集程序并发读写同一 db：用 WAL + busy_timeout 避免锁冲突
