# mtconnect_collect

基于 **MTConnect** 的多系统机床数据采集（FANUC / MAZAK / 通用 SHDR），独立于现有
cnc_monitor 项目，仅依赖 `third_party/` 下的 fwlib / adapter-Version / mtconnect-agent /
sqlite3 源码与库。

## 架构（多系统可扩展）

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
│  mtc_stats (poll/report)  运行时间/产量/分产品，系统无关      │
│  未来: MES / 大屏 / 现有 GUI                                │
└────────────────────────────────────────────────────────────┘
```

## 机器清单（jichuang.txt v2）

```
# name,type,ip,port[,config]
#   type: FANUC | MAZAK | SIM | SHDR
ZXJ03,FANUC,192.168.11.186,8193          ; FANUC → FOCAS 端口
MZK01,MAZAK,192.168.11.200,7878          ; MAZAK → MTConnect 端口
SIM01,SIM,127.0.0.1,7878                  ; 可控模拟机床 (cnc_sim)
BRIDGE,SHDR,192.168.10.50,7878            ; 远程 SHDR 透传
```

- **新增系统**：① 写采集器（输出标准 SHDR，参考 `src/fanuc` / `src/mazak`）
  ② 建设备模板 `devices/<type>.xml` ③ genconfig/start.bat 自动按 type 分发。
- **FANUC 独立配置**：每台可配 `config` 列指向独立 adapter.ini
  （宏变量/PMC/参数按机床定制），默认生成到 `agent/adapters/<name>/adapter.ini`。

## 目录结构

| 路径 | 说明 |
|------|------|
| `src/fanuc/` | FANUC 采集器源码（FOCAS2 → SHDR，含程序注释 `cnc_rdprogdir3` 快路径） |
| `src/mazak/` | MAZAK 采集器源码（MTConnect 拉取 → SHDR） |
| `devices/` | 设备模型模板：`fanuc.xml` / `mazak.xml` / `shdr.xml` / `sim.xml` |
| `tools/genconfig.c` | 读 jichuang.txt → 生成 Devices.xml（模板渲染）+ agent.cfg + adapters.txt |
| `tools/mtc_stats.c` | 采样入库 + 报表（运行时间/产量/分产品） |
| `tools/cnc_sim.c` / `tools/cnc_sim_ctl.c` | 可控模拟机床（SHDR 推送 + HTTP 控制）+ 控制 CLI |
| `tools/shdr_sim.c` / `tools/mazak_sim.c` | 简单离线模拟器（推送模式 / Mazak 拉取模式） |
| `bin/` | 编译产物 + Fwlib32.dll |
| `agent/` | agent.exe + 生成配置 |

## 使用

```bat
build.bat                 % 编译全部（MSVC x86）
start.bat                 % 读 jichuang.txt → 启动各类型采集器 + agent
status.bat                % 状态表格
stop.bat                  % 停止
test.bat                  % 离线演示（全部用 SIM 模拟）
mtc_stats.exe poll 5000 5 stats.db     % 采样入库
mtc_stats.exe report stats.db 1800     % 报表
```

访问 http://127.0.0.1:5000/{probe,current,sample}

## 可控模拟机床 cnc_sim

`cnc_sim.exe` 模拟一台 3 轴加工中心（FANUC 风格）：内置 3 个加工程序
（O1000/O2000/O3000），按程序逐段执行（快移/进给/主轴/结束），输出真实感的
轴位置/负载、进给、主轴转速/负载、件数、模式/状态、报警条件等标准 SHDR 数据，
并可通过本地 HTTP 接口控制机床状态。

```bat
cnc_sim.exe <SHDR端口> [控制端口] [采样ms] [名称]
REM 默认: SHDR=7878, 控制端口=SHDR+2000, 500ms, 名称 SIM01
```

控制接口（仅本机）：

```bat
REM 查看状态 / 命令帮助
curl http://127.0.0.1:9878/state
curl http://127.0.0.1:9878/
REM 发送控制命令（JSON 或 query 均可）
curl -X POST http://127.0.0.1:9878/control -d "{\"cmd\":\"start\"}"
curl "http://127.0.0.1:9878/control?cmd=start"
```

常用命令（也可用配套 CLI：`cnc_sim_ctl.exe <控制端口> <命令> ...`）：

| 命令 | 说明 |
|------|------|
| `start` / `stop` / `hold` / `resume` / `reset` | 启动/停止/保持/继续/复位 |
| `estop` / `estop_release` | 急停 / 解除急停 |
| `mode <AUTOMATIC\|MANUAL\|MDI>` | 切换模式 |
| `program <O1000\|O2000\|O3000>` | 切换加工程序（产品注释随之变化） |
| `alarm <none\|spindle\|servo\|overtravel\|overheat\|comms\|logic\|motion\|system>` | 触发/清除报警（条件置 FAULT、执行中断） |
| `jog <axis> <dir> <dist>` | 手动移动，如 `jog X + 20` |
| `mdi <axis> <dist>` | 单段 MDI 移动 |
| `set <key> <value>` | `Fovr` / `SspeedOvr` / `part_required` / `part_total` / `part_current` / `spindle` |
| `setpos <axis> <value>` | 直接设置坐标 |

示例：

```bat
bin\cnc_sim_ctl.exe 9878 start
bin\cnc_sim_ctl.exe 9878 mode MANUAL
bin\cnc_sim_ctl.exe 9878 jog X + 20
bin\cnc_sim_ctl.exe 9878 alarm spindle
bin\cnc_sim_ctl.exe 9878 program O2000
bin\cnc_sim_ctl.exe 9878 set Fovr 120
```

`start.bat` 启动时会对每个 SIM 机器自动拉起 `cnc_sim.exe`，控制端口 = SHDR 端口 + 2000
（如 SIM01 的 SHDR 为 7888，则控制端口为 9888），并打印在启动日志里。

## 加工统计口径

| 统计项 | 数据来源 |
|--------|---------|
| 运行时间 | `execution`=ACTIVE + `mode`=AUTOMATIC + `tmmode`=0（FANUC；Mazak 无 tmmode 则按前两项） |
| 产量 | `part_total`（FANUC 参数 #6712）差值 |
| 分产品 | 程序注释 `programInfo` 分组 |

## Web 数据报表

访问 **http://127.0.0.1:8088**（webserver.exe 托管前端 + REST API，数据源 stats.db）。

页面（React + Ant Design + ECharts）：

| 页面 | 功能 |
|------|------|
| 总览 | KPI 卡片（机床数/总加工时长/总产量/在线加工中）+ 24h 各机床加工时长与产量图 + 实时状态 + 汇总表 |
| 加工统计 | 可选**时间段 + 机床**（RangePicker/Select/分桶粒度）→ 加工时间柱状图 + 产量折线图 + 分产品产量表 |
| 实时监控 | 各机床状态卡片（5 秒自动刷新：模式/程序/产品/累计件数） |

API 接口文档：**`web/api.md`**（/api/health、/api/machines、/api/stats/summary|machining|production|products、/api/live/current、静态托管）。

```bat
start_web.bat     % 启动 Web 服务（独立窗口，可关闭终端）
run_all.bat       % 一键启动全部（采集 + agent + 采样 + Web）
```

开发模式（改前端代码热更新）：
```bat
cd web
npm run dev        % http://localhost:5173（已配置 /api 代理到 :8088）
```
生产：`cd web && npm run build` 后由 webserver 托管 `web/dist`。

- FANUC：每台 FOCAS 单连接限制，启用本采集后勿再用 cnc_sampler.exe 直连同一台。
- MAZAK：采集器按拉取模式每轮短连接（与 mazak_ops.c 参考客户端一致），
  数据项映射在 `src/mazak/mazak_adapter.cpp mapTag()`，可按实际 Mazak 返回调整。
- 端口：SHDR 默认 7878+；agent HTTP 默认 5000，`start.bat 5001 9000` 可自定义。
## 注意事项
