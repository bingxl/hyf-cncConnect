# mtconnect_collect

基于 **MTConnect** 的多系统机床数据采集与统计系统：FANUC（FOCAS2）、MAZAK
（MTConnect 拉取）、离线 SIM 模拟、远程 SHDR 透传。采集器统一输出标准 SHDR，
由 MTConnect agent 汇聚成 HTTP 接口；`mtc_stats` 增量入库；`webserver` 提供
Web 报表与 REST API。

独立于 `cnc_monitor` 项目，依赖 `../third_party/` 下的 fwlib、mtconnect-agent、
sqlite3 源码与库。

## 架构

```
┌── 采集器层：每台机器一个进程 → 统一 SHDR 输出 ──────────────┐
│  FANUC:  fanuc_adapter.exe <ip> <focas端口> <SHDR端口>     │
│          FOCAS2 (fwlib32) 直连，输出标准 SHDR               │
│  MAZAK:  mazak_adapter.exe <ip> <mtconnect端口> <SHDR端口> │
│          MTConnect 拉取模式(Probe/Streams) → SHDR          │
│  SIM:    cnc_sim.exe <SHDR端口> <控制端口>  (可控模拟机床)  │
│  SHDR:   透传——agent 直连远程 SHDR，无需本地进程           │
└──────────────────────────┬─────────────────────────────────┘
                           │ 127.0.0.1:7878+n
┌── 汇聚层 ─────────────────▼─────────────────────────────────┐
│  agent.exe   Devices.xml 由 devices/<type>.xml 模板生成      │
│  HTTP: /probe /current /sample   JSON/XML                   │
└──────────────────────────┬─────────────────────────────────┘
                           │
┌── 应用层 ─────────────────▼─────────────────────────────────┐
│  mtc_stats (stream/poll/report/prune) 增量入库 + 统计        │
│  webserver (REST + Web 报表)  报警/通知（webhook）           │
│  数据库抽象层 src/db（SQLite 现役，预留 MySQL/PostgreSQL）   │
└────────────────────────────────────────────────────────────┘
```

## 目录结构

| 路径 | 说明 |
|------|------|
| `src/fanuc/` | FANUC 采集器（FOCAS2 → SHDR，程序注释走 `cnc_rdprogdir3` 快路径） |
| `src/mazak/` | MAZAK 采集器（MTConnect 拉取 → SHDR，映射在 `mapTag()`） |
| `src/adapter.cpp` 等 | 共享 SHDR 适配器框架（socket 服务、datum 注册、条件列表） |
| `src/db/` | 数据库抽象层：`db.hpp/cpp`（接口+SQLite 后端）、`stats_db.*`（建表/upsert/报警/保留） |
| `src/webserver/webserver.cpp` | Web 服务（winsock + 抽象层查询，托管 `web/dist`） |
| `devices/` | 设备模型模板：`fanuc.xml` / `mazak.xml` / `shdr.xml` / `sim.xml` |
| `tools/genconfig.c` | 读 `jichuang.txt` → 生成 `Devices.xml` / `agent.cfg` / `adapters.txt` |
| `tools/mtc_stats.cpp` | 采样入库（stream 增量 / poll 快照）+ 报表 + 保留清理 |
| `tools/mtc_ctl.cpp` | 服务控制台：start / test / stop / status / poll / web / report |
| `tools/cnc_sim.c` / `cnc_sim_ctl.c` | 可控模拟机床（SHDR 推送 + HTTP 控制）+ 控制 CLI |
| `tools/shdr_sim.c` / `mazak_sim.c` | 简单离线模拟器（推送 / Mazak 拉取） |
| `web/` | 前端（Vite + React + TS + Ant Design + ECharts） |
| `bin/` | 编译产物 + Fwlib32.dll |
| `agent/` | `agent.exe`（第三方）+ 生成的配置 |
| `log/` | 运行日志（由 mtc_ctl 重定向） |
| `stats.db` | SQLite 统计库（samples / stream_state / alarms） |

---

## 一、配置

### 1.1 机器清单 `jichuang.txt`（必配）

每行一台机器，格式：`name,type,ip,port[,config]`，`#` / `;` 开头为注释。

```
# name,type,ip,port[,config]
#   type: FANUC | MAZAK | SIM | SHDR
ZXJ03,FANUC,192.168.11.186,8193          ; FANUC → FOCAS 端口
MZK01,MAZAK,192.168.11.200,7878          ; MAZAK → MTConnect 端口
SIM01,SIM,127.0.0.1,7878                  ; 可控模拟机床 (cnc_sim)
BRIDGE,SHDR,192.168.10.50,7878            ; 远程 SHDR 透传
```

- **FANUC 独立配置**：第 5 列指向该机的 `adapter.ini`；缺省由 genconfig 生成到
  `agent/adapters/<name>/adapter.ini`。`[macros]`（如 `part_current=3901`、
  `part_required=3902`）、`[pmc]`（SspeedOvr/Fovr 等）、`[params]`
  （产量总件数 `part_total=6712`）可按机床定制。
- **新增系统类型**：① 写采集器输出标准 SHDR（参考 `src/fanuc`/`src/mazak`）
  ② 建设备模板 `devices/<type>.xml` ③ `mtc_ctl start` 自动按 type 分发。
- 修改后重启生效：`mtc_ctl stop && mtc_ctl start`。

### 1.2 端口规划（默认）

| 端口 | 用途 | 配置 |
|------|------|------|
| 7878+n | 各采集器 SHDR 推送端口 | `mtc_ctl start [http_port] [shdr_base]` |
| 5000 | MTConnect agent HTTP（/probe /current /sample） | `--http-port` |
| 8088 | Web 报表 + REST API | `--web-port` |
| SHDR+2000 | cnc_sim 控制端口（仅 SIM） | 自动 |
| 5173 | 前端开发服务器 | `web/vite.config.ts` |

### 1.3 采集与统计配置（mtc_stats stream 参数）

```
bin\mtc_stats.exe stream [http_port] [db] [interval_ms] [prune_days] [alert_url] [alert_min]
```

| 参数 | 默认 | 说明 |
|------|------|------|
| interval_ms | 5000 | 采样/入库间隔（毫秒） |
| prune_days | 0 | >0 时每小时自动清理早于该天数的采样与已关闭报警 |
| alert_url | - | webhook 通知地址（POST `{"text":"..."}`，兼容钉钉/企业微信机器人） |
| alert_min | 60 | 持续异常重复告警间隔（分钟） |

触发通知的场景：机床离线 / 持续离线 / 恢复在线、采集服务（agent）断链 / 恢复。

### 1.4 数据库

- 默认 `stats.db`（SQLite），表：`samples`（ts,machine,execution,mode,tmmode,
  program,comment,part_total,power）、`stream_state`（增量续传 seq/instance_id）、
  `alarms`（报警条件/急停历史）。
- 访问统一走 `src/db/` 抽象层：`Database` / `Statement` 接口 + SQLite 后端，
  upsert 等方言 SQL 集中在 `stats_db.cpp`；迁移 MySQL/PostgreSQL 时新增后端并在
  `db::open()` 分发即可，业务代码无需改动（Config 已预留 host/port/user/password）。

### 1.5 前端配置

- 开发：`web/vite.config.ts` 将 `/api` 代理到 `http://127.0.0.1:8088`。
- 生产：前端构建产物 `web/dist` 由 webserver 直接托管。

---

## 二、编译

### 2.1 环境要求

- Windows + Visual Studio Build Tools（含 **x86 32 位** 组件，Fwlib32.lib 是 32 位）。
- 依赖目录（本仓库的上一级 `third_party/`）：
  - `third_party/fwlib/`（Fwlib32.dll / Fwlib32.lib / fwlib32.h）
  - `third_party/mtconnect-agent/bin/agent.exe`（缺省时 build.bat 会告警，需手动放入 `agent/`）
  - `third_party/sqlite3/`（sqlite3 合并源码）
- 前端：Node.js 18+ / npm。

### 2.2 编译后端

```bat
build.bat
```

首次运行自动探测 vcvarsall 并缓存到 `.vsbuild_cache`。产物在 `bin/`：

```
fanuc_adapter.exe  mazak_adapter.exe  genconfig.exe
shdr_sim.exe       mazak_sim.exe      cnc_sim.exe  cnc_sim_ctl.exe
mtc_stats.exe      webserver.exe      mtc_ctl.exe
```

### 2.3 编译前端

```bat
cd web
npm install
npm run build        % 产物输出到 web/dist（webserver 托管）
```

---

## 三、部署

### 3.1 生产部署步骤

1. 将整个项目目录放到采集服务器（Windows，建议固定路径）。
2. 编辑 `jichuang.txt` 填好全部机床；核对端口规划无冲突。
3. 执行 `build.bat`，确认 10 个 exe 生成、`agent\agent.exe` 存在。
4. 启动：`bin\mtc_ctl.exe start 5000 7878`
5. 验证：
   - `bin\mtc_ctl.exe status` —— 各服务进程 + 机床连接表
   - 浏览器打开 `http://<服务器IP>:8088`（Web 报表）
   - `http://<服务器IP>:5000/probe`（agent 正常）

### 3.2 服务化 / 开机自启

`mtc_ctl start` 以隐藏窗口启动全部服务（不依赖当前终端）。如需开机自启，
二选一：

- 任务计划程序：开机运行 `bin\mtc_ctl.exe start`（推荐，简单）；
- NSSM 包装：把 `mtc_ctl start` / `mtc_ctl stop` 注册为 Windows 服务，
  获得崩溃自动重启能力。

### 3.3 数据可靠性与备份

- 保留策略：`start` 默认 `prune_days=90`，每小时自动清理；也可手动
  `bin\mtc_stats.exe prune stats.db 90`。
- 备份：建议每日停采（或短暂 `mtc_ctl stop`）后复制 `stats.db` + `log/` 归档；
  不停机备份可用 sqlite3 的 `.backup`/`VACUUM INTO`。保留 N 天滚动。
- 时间同步：服务器与车间设备统一 NTP，否则加工时长统计会漂移。
- 采集中断感知：agent 重启后 stream 会自动 `/current` 快照重建；若出现数据缺口，
  查看 `log\stats_poll.log` 中 `sample request failed / re-snapshotting` 记录。

### 3.4 安全建议

- 当前 webserver / agent **无鉴权**，请务必用防火墙/网段隔离，只对办公网暴露 8088；
  对外访问建议加 HTTPS 反向代理 + 登录。
- FOCAS 端口（8193 等）仅允许采集服务器访问机床。

### 3.5 迁移到 MySQL（可选）

1. 在 `src/db/` 增加 MySQL 后端（实现 `Database`/`Statement`，
   MySQL 的 upsert SQL 已写在 `stats_db.cpp`）。
2. `db::open()` 按 `Config::backend` 分发；`mtc_stats`/`webserver` 启动参数
   传入连接配置。
3. 建表后迁移数据：`samples`（按月分区 + 索引 `(machine,ts)`）、`alarms`、
   `stream_state`。

### 3.6 升级流程

```bat
bin\mtc_ctl.exe stop
build.bat
bin\mtc_ctl.exe start
```

---

## 四、运行

### 4.1 服务控制台 `mtc_ctl`

| 命令 | 说明 |
|------|------|
| `mtc_ctl start [http_port] [shdr_base]` | 停残留 → genconfig → 采集器 → agent → poll → web（全部后台隐藏） |
| `mtc_ctl test [http_port] [shdr_base]` | 离线演示：所有机台用 cnc_sim，等待后打印状态表 |
| `mtc_ctl stop` | 按 root 路径精确停止本项目的 agent/采集器/模拟器/poll/web |
| `mtc_ctl status [http_port]` | 服务进程表 + 机床连接表 + web health |
| `mtc_ctl poll` | 单独后台启动统计采集（可带 `--alert-url` 等） |
| `mtc_ctl web` | 单独后台启动 webserver |
| `mtc_ctl report [bucket] [from] [to]` | 统计报表（默认 24h / 1800s） |

常用选项：`--http-port`、`--shdr-base`、`--web-port`、`--root`、`--jichuang`、
`--agent-dir`、`--db`、`--interval-ms`、`--prune-days`、`--alert-url`、
`--alert-min`、`--no-poll`、`--no-web`、`--visible`。

### 4.2 统计工具 `mtc_stats`

```bat
bin\mtc_stats.exe stream 5000 stats.db 5000 90     % 增量采集（推荐）
bin\mtc_stats.exe poll 5000 5 stats.db             % 旧版 /current 快照
bin\mtc_stats.exe report stats.db 1800             % 报表（默认 24h）
bin\mtc_stats.exe prune stats.db 90                % 手动保留清理
```

### 4.3 Web 报表

访问 `http://<服务器>:8088`：

| 页面 | 功能 |
|------|------|
| 总览 | KPI（机床数/24h 加工时长/产量/在线加工中）+ 图表 + 利用率 + 汇总表 |
| 加工统计 | 全部机台或单机 + 日期范围（仅日期）→ 按班次图形报表（各班次加工时长/产量、利用率、分产品件数堆叠图）+ 各班次机台明细（利用率/产量/产品件数） |
| 实时监控 | 状态筛选 + 各机床状态卡片（5 秒刷新） |
| 报警 | 当前报警（10 秒刷新）+ 历史报警（按机床/时间筛选） |

API 文档：`web/api.md`（/api/health、/api/machines、/api/stats/*、
/api/alarms/current|history、/api/live/current、静态托管）。

### 4.4 可控模拟机床 cnc_sim

```bat
cnc_sim.exe <SHDR端口> [控制端口] [采样ms] [名称]
```

控制接口（仅本机，控制端口 = SHDR + 2000）：

```bat
curl http://127.0.0.1:9878/state
curl -X POST http://127.0.0.1:9878/control -d "{\"cmd\":\"start\"}"
bin\cnc_sim_ctl.exe 9878 start
bin\cnc_sim_ctl.exe 9878 alarm spindle
bin\cnc_sim_ctl.exe 9878 program O2000
```

命令：`start/stop/hold/resume/reset`、`estop/estop_release`、`mode`、`program`
（O1000/O2000/O3000）、`alarm`（spindle/servo/overtravel/overheat/comms/logic/
motion/system/none）、`jog`、`mdi`、`set`（Fovr/SspeedOvr/part_required/part_total/
part_current/spindle）、`setpos`。每个程序内置多个产品名（programInfo），
每加工 10 件自动切换下一产品。

---

## 五、维护

### 5.1 日常操作

```bat
bin\mtc_ctl.exe status      % 查看各服务与机床连接
bin\mtc_ctl.exe stop        % 停止全部
bin\mtc_ctl.exe start       % 启动全部（改配置后重启）
```

日志位置 `log\`：

| 文件 | 内容 |
|------|------|
| `agent_console.log` | MTConnect agent |
| `<机器名>.log` | 各采集器 / 模拟器 |
| `stats_poll.log` | 统计采集（stream 日志：seq 推进、快照、prune、告警） |
| `webserver.log` | Web 服务 |

### 5.2 数据维护

- 保留清理：`stream` 带 `prune_days` 自动执行；手动 `mtc_stats prune stats.db 90`。
- 备份/恢复：复制 `stats.db`；恢复时直接覆盖同名文件后重启 poll/web。
- 库增长监控：`status` 中 `/api/health` 的 `db_rows` 可看采样行数。

### 5.3 告警与报警

- 报警入库：条件（Fault/Warning/Failed）与急停自动写入 `alarms` 表，
  Web「报警」页可查当前/历史。
- 通知：`mtc_ctl poll --alert-url <webhook>` 或 `mtc_ctl start --alert-url ...`，
  机床离线/恢复、采集断链/恢复会 POST `{"text":"..."}` 到 webhook
  （钉钉/企业微信机器人可加自定义包装）。

### 5.4 常见故障排查

| 现象 | 排查 |
|------|------|
| status 显示 agent 不可达 | 确认 `mtc_ctl start` 已执行；`log\agent_console.log`；端口被占用则换 `--http-port` |
| 某台机床一直 UNAVAILABLE | 采集器日志 `<机器名>.log`；FOCAS 单连接限制（勿同时跑 cnc_sampler）；网络/端口可达性 |
| stream 反复 re-snapshotting | agent 重启过；正常现象，会重建基线。频繁出现则查 agent 稳定性 |
| 报表/Web 无数据 | `status` 看 mtc_stats 是否运行、`stats_poll.log` 是否在写 seq；`stats.db` 是否有行 |
| DB 无限增长 | 确认 stream 带 `prune_days`；手动 `mtc_stats prune stats.db 90` |
| 走芯机产量不准 | 核对计数口径（#6712 在 M30 时 +1），参考「加工统计口径」与逐台批量校验 |
| webserver 端口被占 | `--web-port` 换端口；`mtc_ctl stop` 后再启动 |

### 5.5 注意事项

- FANUC：每台 FOCAS 单连接限制，启用本采集后勿再用 cnc_sampler.exe 直连同一台。
- MAZAK：采集器每轮短连接拉取，数据项映射在 `mapTag()`，按实际 Mazak 返回调整。
- 端口：SHDR 默认 7878+；agent HTTP 默认 5000，`mtc_ctl start 5001 9000` 可自定义。

---

## 加工统计口径

| 统计项 | 数据来源 |
|--------|---------|
| 运行时间 | `execution`=ACTIVE + `mode`=AUTOMATIC；相邻样本时间差累加，间隔 >90s 视为断联不计 |
| 开机时间 | `Availability`=AVAILABLE（power=1）；旧数据 NULL 视为开机 |
| 利用率 | 运行时间 / 开机时间 |
| 产量 | `part_total` 正增量求和（相邻样本上升则累加，兼容计数清零/重启） |
| 分产品 | 程序注释 `programInfo` 分组，产量按各产品区间累计 |

> 走芯机/自动送料机注意：#6712 在 M02/M30（或参数 6710 指定 M 码）时 +1，
> 适合“一件一循环”程序；双程序/多件循环/中间清零会偏，建议逐台做批量校验。
