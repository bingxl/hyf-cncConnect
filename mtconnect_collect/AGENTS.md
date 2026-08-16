# AGENTS.md - MTConnect CNC Data Collection

## Project Overview

A multi-system CNC data collection pipeline based on **MTConnect**. Each machine runs a
per-system collector that emits standard SHDR (push) data; an MTConnect agent aggregates
all collectors and exposes HTTP `/probe` `/current` `/sample`; a polling/statistics tool
stores samples in SQLite and reports machining time / produced parts / per-product output.

Supports FANUC (FOCAS2), MAZAK (MTConnect pull mode), offline SIM, and remote SHDR
passthrough. Independent from the `cnc_monitor` project (a sibling under `F:\code\cnc_test`).

本项目使用场景: 
1. 核心目的：数控机床的数据采集，给公司提供直观的数据图标，展示时间段内每台机床的加工数量以及实际开机时间与加工时间；
2. 机床状况：公司共70多台品牌不一的机床分布在多个部门，cnc部门的36台cnc机床，数车部分为走芯机的15台机器与走刀机的9台数控车床3台车铣复合机床

公司上班情况：
分两班倒，白班上班时间为周一到周六 8:30-20:30, 星期天不上班或者8:30-17:00
夜班周一到周六是 20:30-次日8:30 ， 星期天不上班


## Architecture

```
┌── Collector layer: one process per machine → unified SHDR output ──┐
│  FANUC:  fanuc_adapter.exe <ip> <focas-port> <shdr-port>          │
│          FOCAS2 (fwlib32) direct → SHDR                            │
│  MAZAK:  mazak_adapter.exe <ip> <mtconnect-port> <shdr-port>      │
│          MTConnect pull (Probe/Streams) → SHDR                     │
│  SIM:    cnc_sim.exe <shdr-port> <control-port>  (controllable)    │
│  SHDR:   passthrough — agent connects to remote SHDR directly     │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ 127.0.0.1:7878+n
┌── Aggregation layer ──────▼─────────────────────────────────────────┐
│  agent.exe   Devices.xml generated from devices/<type>.xml templates│
│  HTTP: /probe /current /sample   JSON/XML                           │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
┌── Application layer ──────▼─────────────────────────────────────────┐
│  mtc_stats (stream/poll/report)  running time / production / product │
│  webserver.exe (optional web dashboard, see web/ — out of scope)    │
└──────────────────────────────────────────────────────────────────────┘
```

## Key Files

| File | Purpose |
|------|---------|
| `src/adapter.cpp/hpp` | Adapter base class: socket SHDR server, datum registry, changed-value push loop, initial-data send |
| `src/device_datum.cpp/hpp` | MTConnect datum types: `Event`, `Sample`, `Condition`, `Execution`, `ControllerMode`, `PathPosition`, `Availability`, etc. |
| `src/server.cpp/hpp`, `src/client.cpp/hpp` | SHDR TCP server / client handling, heartbeats, framing |
| `src/service.cpp/hpp` | `MTConnectService` main loop + optional Windows service install (`install`/`run`/`debug` commands) |
| `src/string_buffer.cpp`, `src/condition_list.cpp`, `src/logger.cpp`, `src/minIni.c` | Shared helpers (SHDR line buffer, alarm condition list, logging, INI parsing) |
| `src/fanuc/fanuc_adapter.cpp/hpp` | FANUC FOCAS2 collector: connects, configures axes/spindles/macros/PMC/params, gathers positions/status/alarms/macro/PMC |
| `src/fanuc/FanucAdapter.cpp` | FANUC adapter entry point (`main`) |
| `src/fanuc/adapter.ini` | FANUC per-machine config: `[macros]` `[pmc]` `[params]` (e.g. part_total=#6712) |
| `src/mazak/mazak_adapter.cpp` | MAZAK MTConnect-pull collector; `mapTag()` maps remote `tag|value` lines to datum names |
| `src/webserver/webserver.cpp` | Optional web server (C++ rewrite, hosts `web/dist`, REST API over stats.db) |
| `src/db/db.hpp/cpp` + `src/db/stats_db.*` | 数据库抽象层：`Database`/`Statement` 接口 + SQLite 后端；统计建表/upsert/保留清理的后端差异集中在此，便于后续迁移 MySQL/PostgreSQL |
| `tools/genconfig.c` | Reads `jichuang.txt` → generates `agent/Devices.xml` (from templates), `agent/agent.cfg`, `agent/adapters.txt`, per-machine `adapter.ini` |
| `tools/mtc_stats.cpp` | `stream` (增量拉取 agent `/sample?from=<seq>` → SQLite `samples`)、`poll`（兼容旧 `/current` 快照）和 `report`（加工时长分桶 / 产量差值 / 分产品） |
| `tools/shdr_sim.c` | Offline SHDR simulator (push mode, fake FANUC-style data) |
| `tools/cnc_sim.c` / `tools/cnc_sim_ctl.c` | Controllable realistic CNC simulator (SHDR push + local HTTP control API) + control CLI |
| `tools/mazak_sim.c` | Offline Mazak pull-mode simulator (Streams/Probe/Changes commands) |
| `devices/fanuc.xml`, `devices/mazak.xml`, `devices/sim.xml`, `devices/shdr.xml` | Device model templates with placeholders `%NAME% %UUID% %IP% %PORT% %SHDRPORT%` |
| `jichuang.txt` | Machine list v2: `name,type,ip,port[,config]` |
| `build.bat` | Build everything (MSVC x86), caches vcvarsall path in `.vsbuild_cache` |
| `tools/mtc_ctl.cpp` | 服务控制台（`bin\mtc_ctl.exe`）：`start`/`test`/`stop`/`status`/`poll`/`web`/`report`，替代原 start/stop/status/test/view/start_poll/start_web/start_hidden/run_all 脚本 |

## Machine List (jichuang.txt v2)

```
# name,type,ip,port[,config]
#   type: FANUC | MAZAK | SIM | SHDR
ZXJ03,FANUC,192.168.11.186,8193          ; FANUC → FOCAS port
MZK01,MAZAK,192.168.11.200,7878          ; MAZAK → MTConnect port
SIM01,SIM,127.0.0.1,7878                  ; controllable simulator (cnc_sim.exe)
BRIDGE,SHDR,192.168.10.50,7878            ; remote SHDR passthrough
```

- **Adding a system type**: ① write a collector emitting standard SHDR (see `src/fanuc`,
  `src/mazak`) ② add a device template `devices/<type>.xml` ③ genconfig/mtc_ctl dispatch
  automatically by `type`.
- **FANUC per-machine config**: optional 5th column points to a custom `adapter.ini`
  (macro/PMC/param overrides). Default generated to `agent/adapters/<name>/adapter.ini`.

## Ports & Data Flow

- SHDR ports: `shdr_base_port` default **7878**, one per machine (`7878+i`).
- Agent HTTP: default **5000** (`mtc_ctl start [http_port] [shdr_base]` to customize).
- Agent reads adapters via `agent/agent.cfg` `Adapters {}` blocks (host 127.0.0.1 + shdr port).
  `SHDR` type bypasses local collectors — the agent connects to the remote host:port directly.

## Build

```bat
build.bat
```

- MSVC (`cl.exe`) via VS Build Tools, **x86** target (required by `Fwlib32.lib`).
- Outputs in `bin/`: `fanuc_adapter.exe`, `mazak_adapter.exe`, `genconfig.exe`,
  `shdr_sim.exe`, `mazak_sim.exe`, `cnc_sim.exe`, `cnc_sim_ctl.exe`,
  `mtc_stats.exe`, `webserver.exe`, `mtc_ctl.exe`.
- `Fwlib32.dll` copied to `bin/`; `agent.exe` copied from `../third_party/mtconnect-agent/bin`
  if present (warns otherwise — drop it into `agent/` manually).
- SQLite amalgamation built once into `bin/sqlite3.obj` (`SQLITE_THREADSAFE=0`).
- `webserver.exe` is a C++ rewrite of the stats web server (`src/webserver/webserver.cpp`).
- Build ends by deleting stray `*.obj` left in the project root (MSVC emits them to cwd).
- No separate lint/format/typecheck step; MSVC warnings treated as warnings only.

## Usage

```bat
build.bat                    % compile all (MSVC x86)
bin\mtc_ctl.exe start 5000 7878   % genconfig → collectors → agent → poll → web
bin\mtc_ctl.exe test 5000 7878    % offline demo (all SIM) + status table
bin\mtc_ctl.exe status [5000]     % process table + 机床连接表 + web health
bin\mtc_ctl.exe stop              % 停止 root 下的 agent/采集器/模拟器/poll/web
bin\mtc_ctl.exe report [bucket] [from] [to]   % 统计报表（默认 24h / 1800s）
bin\mtc_ctl.exe poll --alert-url <url> --alert-min 60   % 只启动统计采集
bin\mtc_ctl.exe web               % 只启动 webserver
bin\mtc_stats.exe stream 5000 stats.db 5000 90  % incremental stream (90d retention, hourly prune)
bin\mtc_stats.exe poll 5000 5 stats.db          % legacy /current snapshot
bin\mtc_stats.exe report stats.db 1800          % report
bin\mtc_stats.exe prune stats.db 90             % manual retention cleanup
```

Check agent: http://127.0.0.1:5000/{probe,current,sample}

## Controllable Simulator (cnc_sim)

`cnc_sim.exe` simulates a 3-axis machining-center style FANUC machine and pushes
realistic SHDR data on `<shdr_port>` for the agent: program cycle (rapid / feed /
spindle / end), axis positions / commanded / loads, feed, spindle speed / load,
part counts (`part`, `part_current`, `part_required`, `part_total`), `tmmode`,
execution / mode / estop / alarm conditions (`servo`, `spindle`, `Xtravel`, ...).

Machine state is controlled over a local HTTP API on `<control_port>` (default
`shdr_port + 2000`):

```
GET  /                  -> command help
GET  /state             -> machine state (JSON)
POST /control           -> {"cmd":"start", ...}  (GET /control?cmd=... also works)

Commands:
  start | stop | hold | resume | reset
  estop | estop_release
  mode <AUTOMATIC|MANUAL|MDI>
  program <O1000|O2000|O3000>        (3 built-in part programs, product comment switches)
  alarm <none|spindle|servo|overtravel|overheat|comms|logic|motion|system>
  jog <axis> <dir> <dist>            e.g. jog X + 20
  mdi <axis> <dist>                  e.g. mdi Z -5
  set <key> <value>                  Fovr | SspeedOvr | part_required |
                                     part_total | part_current | product_index | spindle
  setpos <axis> <value>
```

Convenience CLI: `cnc_sim_ctl.exe <control_port> <command> [args...]`
(e.g. `cnc_sim_ctl 9878 start`, `cnc_sim_ctl 9878 alarm spindle`).
`mtc_ctl start` / `mtc_ctl test` launch `cnc_sim.exe` for SIM entries (control
port = SHDR port + 2000, printed at startup). Each program has multiple product names
(`programInfo`); the simulator switches to the next product every 10 completed
parts (`PRODUCT_SWITCH_PARTS`), and `/state` exposes `product_index` /
`parts_until_switch`.

## Stats Model (mtc_stats)

- `samples` table: `ts, machine, execution, mode, tmmode, program, comment, part_total`
  `PRIMARY KEY(ts, machine)`. Index on `(machine, ts)`.
- `stream`: 流式增量采集 —— 每次 `GET /sample?from=<lastSeq+1>&count=1000` 只拉取新
  事件，按响应事件 `sequence` 续传（`stream_state` 表持久化 seq / instanceId），
  重启或 agent 重启后自动 `/current` 快照重建基线。响应按块读取即时解析，兼容
  长连接流式 agent；`poll` 保留旧 `/current` 全量快照模式。
  `stream` 第 5 个参数 `prune_days>0` 时每小时清理一次过期采样（保留策略）。
- `mtc_stats` / `webserver` 的数据库访问统一走 `src/db/` 抽象层（SQLite 现役）；
  迁移 MySQL 时新增 `db_mysql.cpp` 并在 `db::open()` 分发，upsert 等方言 SQL
  已集中在 `stats_db.cpp`。webserver 每请求独立打开连接（SQLite 单线程编译，
  多线程共享句柄不安全；MySQL 后端可替换为连接池）。
- `alarms` 表：报警条件（Fault/Warning/Failed）与急停（EmergencyStop=TRIGGERED）
  由 stream/poll 采集入库，occurrence 以 (machine,item_id,first_ts) 唯一；
  webserver 提供 `/api/alarms/current`（当前报警）与 `/api/alarms/history`
  （历史），前端「报警」页展示。
- 通知：`stream` 第 6/7 参数 `alert_url` / `alert_min` 启用 webhook 告警
  （机床离线/恢复、采集断链/恢复，POST `{"text":"..."}` 通用格式）。
- `report` commands:
  1. **Machining time** per machine per bucket: sample counted as machining when
     `execution==ACTIVE && mode==AUTOMATIC`（自动模式实际运行时间；tmmode 只作
     展示字段，不再硬性过滤，避免车铣复合 M 模式漏计）；bucket aligned to wall-clock
     `ts/bucket_sec`. MachSec 由相邻样本时间差累加（样本 i 加工中则累加
     `ts[i+1]-ts[i]`，跨桶按边界拆分），对缺行/间隔不均比“样本数×平均间隔”稳健。
  2. **Power-on time & utilization**: `samples.power`（`Availability=AVAILABLE`=1，
     关机/断联=0，旧数据 NULL 视为开机）；开机时间同样按相邻样本时间差累加。
     **利用率 = 实际加工时间 / 开机时间**。相邻样本间隔超过 90s 视为断联，该时段
     不计入开机时间与加工时间。
  3. **Produced parts** = `part_total` positive-increment sum（相邻样本 `part_total`
     上升则累加差值；兼容计数清零/重启，如断电归零；首尾差值会漏算）.
  4. **Per-product** parts: walk samples per machine, group consecutive runs by `comment`
     (program comment), sum `part_total` deltas per product.

## FANUC Adapter Notes

- Datum names must match `devices/fanuc.xml` data item ids: `avail`, `estop`,
  `execution`, `mode`, `line`, `program`, `programInfo`, `block`, `pathFeedrate`,
  `pathPosition`, `part`/`part_current`/`part_required`/`part_total`, `tmmode`,
  condition ids `servo`/`comms`/`logic`/`motion`/`system`/`spindle`, and axis/spindle items.
- `execution` derived from `cnc_statinfo`: `run==3` (STaRT) → ACTIVE; HOLD/Wait → INTERRUPTED;
  STOP → STOPPED; else READY. `mode` from `aut` (5/6 → MANUAL, 0 → MANUAL_DATA_INPUT, else AUTOMATIC).
- `getProgramComment()` uses `cnc_rdprogdir3` fast path (top=program number) with a
  `cnc_rdprogdir2` paged-scan fallback; populates `programInfo` (or "UNKNOWN").
- Axis scale divisors from `cnc_getfigure`; positions from `cnc_rddynamic2` (machine & absolute).
- One FOCAS connection per machine — do not run `cnc_sampler.exe` against the same FANUC
  machine concurrently.

## MAZAK Adapter Notes

- `mazak_adapter.cpp mapTag()` maps the remote Mazak `tag|value` lines to datum names
  (e.g. `Xact`, `part_total`, `execution`, `mode`). Extend `mapTag()` + `devices/mazak.xml`
  together when adding fields.
- New TCP connection per poll round (`connect` → `getData` → `endRound`); socket stays
  AVAILABLE between rounds so the agent retains last values. SO_RCVTIMEO=3000ms skips slow rounds.
- `mazak_sim.exe` is the matching offline test endpoint.

## Conventions & Gotchas

- All collectors reuse the AMT-derived adapter framework in `src/` — keep shared code there
  rather than duplicating in a new collector.
- `.gitignore` covers generated artifacts: `agent/agent.cfg`, `agent/Devices.xml`,
  `agent/adapters.txt`, `adapters.txt`, `log/`, `.vsbuild_cache`, `stats.db`.
- `bin/`, `agent/` binaries and `web/node_modules` are gitignored (check root `.gitignore`).
- 服务启停统一走 `mtc_ctl.exe`（隐藏窗口、日志重定向到 `log/`）；`build.bat` 是唯一保留的批处理。
- `mtc_stats` is a C++ utility — avoid platform-specific libs beyond WinHTTP/SQLite.
