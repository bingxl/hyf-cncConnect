# AGENTS.md - MTConnect CNC Data Collection

## Project Overview

A multi-system CNC data collection pipeline based on **MTConnect**. Each machine runs a
per-system collector that emits standard SHDR (push) data; an MTConnect agent aggregates
all collectors and exposes HTTP `/probe` `/current` `/sample`; a polling/statistics tool
stores samples in SQLite and reports machining time / produced parts / per-product output.

Supports FANUC (FOCAS2), MAZAK (MTConnect pull mode), offline SIM, and remote SHDR
passthrough. Independent from the `cnc_monitor` project (a sibling under `F:\code\cnc_test`).

## Architecture

```
┌── Collector layer: one process per machine → unified SHDR output ──┐
│  FANUC:  fanuc_adapter.exe <ip> <focas-port> <shdr-port>          │
│          FOCAS2 (fwlib32) direct → SHDR                            │
│  MAZAK:  mazak_adapter.exe <ip> <mtconnect-port> <shdr-port>      │
│          MTConnect pull (Probe/Streams) → SHDR                     │
│  SIM:    shdr_sim.exe <shdr-port>            (offline simulation)  │
│  SHDR:   passthrough — agent connects to remote SHDR directly     │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ 127.0.0.1:7878+n
┌── Aggregation layer ──────▼─────────────────────────────────────────┐
│  agent.exe   Devices.xml generated from devices/<type>.xml templates│
│  HTTP: /probe /current /sample   JSON/XML                           │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
┌── Application layer ──────▼─────────────────────────────────────────┐
│  mtc_stats (poll/report)  running time / production / per product    │
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
| `tools/genconfig.c` | Reads `jichuang.txt` → generates `agent/Devices.xml` (from templates), `agent/agent.cfg`, `agent/adapters.txt`, per-machine `adapter.ini` |
| `tools/mtc_stats.c` | `poll` (WinHTTP GET `/current` → SQLite `samples`) and `report` (machining time buckets / production delta / per-product) |
| `tools/shdr_sim.c` | Offline SHDR simulator (push mode, fake FANUC-style data) |
| `tools/mazak_sim.c` | Offline Mazak pull-mode simulator (Streams/Probe/Changes commands) |
| `devices/fanuc.xml`, `devices/mazak.xml`, `devices/sim.xml`, `devices/shdr.xml` | Device model templates with placeholders `%NAME% %UUID% %IP% %PORT% %SHDRPORT%` |
| `jichuang.txt` | Machine list v2: `name,type,ip,port[,config]` |
| `build.bat` | Build everything (MSVC x86), caches vcvarsall path in `.vsbuild_cache` |
| `start.bat` / `stop.bat` / `status.bat` / `status.ps1` | Launch / stop / show connectivity table |
| `test.bat` | Offline demo: SIM for every machine + status table |
| `view.bat` | Run `mtc_stats report` (default last 24h, 30min buckets) |
| `start_poll.bat` / `start_hidden.bat` | Start stats poller only / all services hidden via `tools/hidden_run.vbs` |

## Machine List (jichuang.txt v2)

```
# name,type,ip,port[,config]
#   type: FANUC | MAZAK | SIM | SHDR
ZXJ03,FANUC,192.168.11.186,8193          ; FANUC → FOCAS port
MZK01,MAZAK,192.168.11.200,7878          ; MAZAK → MTConnect port
SIM01,SIM,127.0.0.1,7878                  ; offline simulation
BRIDGE,SHDR,192.168.10.50,7878            ; remote SHDR passthrough
```

- **Adding a system type**: ① write a collector emitting standard SHDR (see `src/fanuc`,
  `src/mazak`) ② add a device template `devices/<type>.xml` ③ genconfig/start.bat dispatch
  automatically by `type`.
- **FANUC per-machine config**: optional 5th column points to a custom `adapter.ini`
  (macro/PMC/param overrides). Default generated to `agent/adapters/<name>/adapter.ini`.

## Ports & Data Flow

- SHDR ports: `shdr_base_port` default **7878**, one per machine (`7878+i`).
- Agent HTTP: default **5000** (`start.bat [http_port] [shdr_base_port]` to customize).
- Agent reads adapters via `agent/agent.cfg` `Adapters {}` blocks (host 127.0.0.1 + shdr port).
  `SHDR` type bypasses local collectors — the agent connects to the remote host:port directly.

## Build

```bat
build.bat
```

- MSVC (`cl.exe`) via VS Build Tools, **x86** target (required by `Fwlib32.lib`).
- Outputs in `bin/`: `fanuc_adapter.exe`, `mazak_adapter.exe`, `genconfig.exe`,
  `shdr_sim.exe`, `mazak_sim.exe`, `mtc_stats.exe`, `webserver.exe`.
- `Fwlib32.dll` copied to `bin/`; `agent.exe` copied from `../third_party/mtconnect-agent/bin`
  if present (warns otherwise — drop it into `agent/` manually).
- SQLite amalgamation built once into `bin/sqlite3.obj` (`SQLITE_THREADSAFE=0`).
- `webserver.exe` is a C++ rewrite of the stats web server (`src/webserver/webserver.cpp`).
- Build ends by deleting stray `*.obj` left in the project root (MSVC emits them to cwd).
- No separate lint/format/typecheck step; MSVC warnings treated as warnings only.

## Usage

```bat
build.bat                    % compile all (MSVC x86)
start.bat                    % read jichuang.txt → collectors + agent
test.bat                     % offline demo (all SIM)
status.bat                   % connectivity table (powershell parses /current)
stop.bat                     % kill agent.exe, *_adapter.exe, *_sim.exe
view.bat [bucket] [from] [to]% report (default last 24h / 1800s buckets)
start_poll.bat               % run mtc_stats poll as background process
start_hidden.bat             % start all services with no console windows
bin\mtc_stats.exe poll 5000 5 stats.db    % sample into sqlite
bin\mtc_stats.exe report stats.db 1800    % report
```

Check agent: http://127.0.0.1:5000/{probe,current,sample}

## Stats Model (mtc_stats)

- `samples` table: `ts, machine, execution, mode, tmmode, program, comment, part_total`
  `PRIMARY KEY(ts, machine)`. Index on `(machine, ts)`.
- `poll`: GET `/current`, parses `<DeviceStream name="...">` blocks with lightweight
  string search (regex-style, not a full XML parser). Requires availability=AVAILABLE.
- `report` commands:
  1. **Machining time** per machine per bucket: sample counted as machining when
     `execution==ACTIVE && mode==AUTOMATIC && tmmode==0`; bucket aligned to wall-clock
     `ts/bucket_sec`. MachSec ≈ machining sample count × interval.
  2. **Produced parts** = `part_total` (#6712) delta in range (first→last, negatives dropped).
  3. **Per-product** parts: walk samples per machine, group consecutive runs by `comment`
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
- Bat scripts use `setlocal enabledelayedexpansion` and `%~dp0` for path independence.
- `mtc_stats report` is a plain C utility — avoid platform-specific libs beyond WinHTTP/SQLite.
