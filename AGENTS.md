# AGENTS.md - CNC Monitor Project

## Project Overview

A C-based CNC monitoring tool that communicates with FANUC CNC controllers via the FOCAS2 protocol using the official FANUC `fwlib32` library. It connects over Ethernet, reads machine status, axis positions, alarms, program info, macro variables, tool offsets, and more. Supports both one-shot data dump and continuous monitoring modes.

## Key Files

| File | Purpose |
|------|---------|
| `main.c` | Interactive CNC monitor entry point |
| `collect.c` | Batch CNC data collector entry point |
| `cnc_ops.h/c` | All CNC machine operations (connect, read data, monitor) |
| `file_io.h/c` | Config file read (`jichuang.txt`) + result file write (`result.txt`) |
| `fwlib/fwlib32.h` | FANUC FOCAS2 library header (16k+ lines) |
| `fwlib/Fwlib32.dll` | Runtime DLL (required at runtime) |
| `fwlib/Fwlib32.lib` | Import lib for MSVC linking |
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
- **`main.c`** / **`collect.c`** — Entry points using the above libraries.

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
- Output: `cnc_monitor.exe` and `cnc_collect.exe`
