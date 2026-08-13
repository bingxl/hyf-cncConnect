# AGENTS.md - CNC Monitor Project

## Project Overview

A C-based CNC monitoring tool that communicates with FANUC CNC controllers via the FOCAS2 protocol using the official FANUC `fwlib32` library. It connects over Ethernet, reads machine status, axis positions, alarms, program info, macro variables, tool offsets, and more. Supports both one-shot data dump and continuous monitoring modes.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | Interactive CNC monitor entry point |
| `collect.c` | Batch CNC data collector entry point |
| `src/cli_sampler/sampler.c` | Resident sampling daemon (`cnc_sampler.exe`) |
| `cnc_ops.h/c` | All CNC machine operations (connect, read data, monitor) |
| `db_ops.h/c` | SQLite database operations (machines, history, batches, machine_samples, machine_latest, archival) |
| `src/imgui_ui/viewmodel/MachiningVm.*` | Machining-stats DB queries (bucket + product grouping), CSV export |
| `src/imgui_ui/view/MachiningView.*` | 「加工统计」GUI page (machine + time range + product tables) |
| `third_party/sqlite3/sqlite3.h/c` | SQLite 3.52.0 amalgamation |
| `mazak_ops.h/c` | Mazak machine operations (MTConnect, FTP, SNMP, raw TCP) |
| `file_io.h/c` | Config file read (`jichuang.txt`) + result file write (`result.txt`) |
| `mazak_test.c` | CLI tool for testing Mazak machine connectivity and data access |
| `third_party/fwlib/fwlib32.h` | FANUC FOCAS2 library header (16k+ lines) |
| `third_party/fwlib/Fwlib32.dll` | Runtime DLL (required at runtime) |
| `third_party/fwlib/Fwlib32.lib` | Import lib for MSVC linking |
| `build.bat` | Build script using MSVC (x86 target) |

## Build

```bat
build.bat
```

Requires Visual Studio Build Tools with x86 (32-bit) support. The Fwlib32.lib is a 32-bit library, so the target must be x86.

## Usage

```
cnc_monitor.exe <IP> [port] [options]

Options:
  <port>             TCP port (default: 8193)
  -monitor [ms]      Continuous monitoring mode (default: 1000ms)
  -parts <cur> <tot> Macro vars for part count (default: #500 #501)

Examples:
  cnc_monitor.exe 192.168.1.100
  cnc_monitor.exe 192.168.1.100 -parts 500 501
  cnc_monitor.exe 192.168.1.100 8193 -monitor 500
  cnc_monitor.exe 192.168.1.100 -parts 500 501 -monitor

Batch collector (reads machines from `jichuang.txt`):
  cnc_collect.exe

Resident sampling daemon (machining-time / product statistics collector):
  cnc_sampler.exe [-interval <secs>] [-log <path>]

  Reads the machine list from `%USERPROFILE%\data-collect\jichuang.txt`
  (or `jichuang.txt` in the working dir). Writes samples to
  `%USERPROFILE%\data-collect\cnc_monitor.db` (`machine_samples` +
  `machine_latest`). Sample interval defaults to config.txt (`interval=`),
  then 5s. Samples older than `retention_days` (default 90) are moved daily
  to `%USERPROFILE%\data-collect\archive\archive_YYYYMMDD.db`.

Mazak machine test tool:
  cnc_mazak_test.exe <IP> [port] [options]

Options:
  -mtconnect        MTConnect protocol test (default, port 7878)
  -raw              Raw TCP protocol test (port 50100)
  -scan             Scan common ports
  -all              Run all tests
  -timeout <ms>     Connection timeout (default: 5000ms)

Examples:
  cnc_mazak_test.exe 192.168.1.100
  cnc_mazak_test.exe 192.168.1.100 -all
  cnc_mazak_test.exe 192.168.1.100 -scan
```

## Architecture

Modularized C project with three layers:

- **`cnc_ops.h/c`** — All CNC machine operations via FOCAS2 API, including:
  - **Connection** (`cnc_connect` / `cnc_disconnect`): Uses `cnc_allclibhndl3` to establish Ethernet connection, `cnc_freelibhndl` to disconnect.
  - **System Info** (`print_system_info`): CNC type, series, version, max axes via `cnc_sysinfo`.
  - **Machine Status** (`print_status`): Mode, run state, emergency, alarm via `cnc_statinfo`.
  - **Axis Positions** (`print_positions`): Absolute, machine, relative, distance via `cnc_absolute`, `cnc_machine`, `cnc_relative`, `cnc_distance`.
  - **Alarms** (`print_alarms`): Alarm status and messages via `cnc_alarm`, `cnc_rdalmmsg`.
  - **Feed/Spindle** (`print_actf_acts`): Actual feedrate and spindle speed via `cnc_actf`, `cnc_acts`.
  - **Program Info** (`print_program_info`): Program number, name, sequence, block count via `cnc_rdprgnum`, `cnc_exeprgname`, `cnc_rdseqnum`, `cnc_rdblkcount`.
  - **Dynamic Data** (`print_dynamic`): Combined read via `cnc_rddynamic2`.
  - **Path Info** (`print_path_info`): Current/total paths via `cnc_getpath`.
  - **Macro Variables** (`print_macro_variables`): Reads `#1`-`#10` via `cnc_rdmacro`.
  - **Tool Offsets** (`print_tool_offsets`): Reads offsets `#0`-`#9` type 0 via `cnc_rdtofs`.
  - **Work Zero Offsets** (`print_work_zero_offsets`): Reads work zero offset `#0` via `cnc_rdzofs`.
  - **Parameters** (`print_parameters`): Reads parameter `#6750` and setting `#0` via `cnc_rdparam`, `cnc_rdset`.
  - **Part Count** (`read_counts` / `get_part_count` / `get_part_count_on_path`): Reads macro variables #3901/#3902 and param #6712.
  - **Monitor Loop** (`monitor_loop`): Continuous polling of status, positions, and part count.
- **`file_io.h/c`** — File I/O: parses `jichuang.txt` (machine list) and writes `result.txt` (collected data).
- **`mazak_ops.h/c`** — Mazak machine operations via MTConnect, raw TCP, FTP, SNMP, OPC UA.
- **`main.c`** / **`collect.c`** / **`mazak_test.c`** — Entry points using the above libraries.
- **`src/cli_sampler/sampler.c`** — Resident sampling daemon (`cnc_sampler.exe`). Keeps one long-lived FOCAS connection per machine, samples status/program/part-count on a configurable interval, writes to `machine_samples`, upserts `machine_latest`, and moves samples older than `retention_days` into a dated archive db. Optional `-interval <secs>` overrides `config.txt`.
- **`MachineVm`/`MachiningView`** — 「加工统计」GUI page: pick machine + time range (今日/昨日/白班/夜班 quick buttons) to view per-30-min bucket machining time and per-product (program comment) grouping, with CSV export.

## Machining-time & product statistics

- Data source: `machine_samples` (raw samples) + `machine_latest` (per-machine latest) in `%USERPROFILE%\data-collect\cnc_monitor.db`.
- **A machine counts as "machining" when `run==3` (STaRT) AND `aut==1` (MEM memory auto) AND `tmmode==0` (T/lathe mode)**.
  - IMPORTANT: `cnc_statinfo`'s `run` is an enum, not a boolean: 0=reset, 1=STOP, 2=HOLD, **3=STaRT**, 4=MSTR. Using `run==1` would exclude all machining time.
- Machining time ≈ (count of machining samples) × sample interval. Buckets are aligned to wall-clock 30-min windows (`ts/1800`).
- Products are grouped by `program_comment` (the product code). Produced parts = `part_total` delta within the range (negative deltas clamped to 0).
- GUI no longer needs direct FOCAS for the stats page — it reads from the sampler's DB.

## Dependencies

- **Fwlib32.dll** - FANUC FOCAS2 library (32-bit). Must be in the same directory as the executable or on `PATH`.
- **fwlib32.h** - Header from the [strangesast/fwlib](https://github.com/strangesast/fwlib) repository.
- **Fwlib32.lib** - Import library for MSVC linking.

## FOCAS2 API Functions Used

| Function | Purpose |
|----------|---------|
| `cnc_allclibhndl3` | Open Ethernet connection |
| `cnc_freelibhndl` | Close connection |
| `cnc_settimeout` | Set communication timeout |
| `cnc_sysinfo` | Read system information |
| `cnc_statinfo` | Read machine status |
| `cnc_absolute` | Read absolute position |
| `cnc_machine` | Read machine position |
| `cnc_relative` | Read relative position |
| `cnc_distance` | Read distance-to-go |
| `cnc_alarm` | Read alarm status |
| `cnc_rdalmmsg` | Read alarm message |
| `cnc_actf` | Read actual feedrate |
| `cnc_acts` | Read actual spindle speed |
| `cnc_rdprgnum` | Read program number |
| `cnc_exeprgname` | Read executing program name |
| `cnc_rdseqnum` | Read sequence number |
| `cnc_rdblkcount` | Read block count |
| `cnc_rddynamic2` | Read dynamic data |
| `cnc_getpath` | Get current path info |
| `cnc_rdmacro` | Read macro variable |
| `cnc_rdtofs` | Read tool offset |
| `cnc_rdzofs` | Read work zero offset |
| `cnc_rdparam` | Read parameter |
| `cnc_rdset` | Read setting |

## Error Handling

All FOCAS2 API calls return a `short` status code. The `focas_error()` function maps codes to human-readable strings. Common codes:

- `EW_OK` (0) - Success
- `EW_BUSY` (-1) - CNC busy
- `EW_RESET` (-2) - CNC reset
- `EW_SOCKET` (-15) - Socket error
- `EW_HANDLE` (-12) - Invalid handle
- `EW_NODLL` (-16) - DLL not found
- `EW_ALARM` (-7) - Alarm occurred
- `EW_STOP` (-8) - CNC not running

## Build Environment

- Compiler: MSVC (`cl.exe`) via Visual Studio Build Tools
- Architecture: x86 (32-bit) - required by `Fwlib32.lib`
- Link: `Fwlib32.lib` and `advapi32.lib`
- Output: `cnc_monitor.exe`, `cnc_collect.exe`, and `cnc_mazak_test.exe`
