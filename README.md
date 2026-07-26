# HYF CNC Connect

A C-based CNC monitoring tool that communicates with FANUC CNC controllers via the FOCAS2 protocol. Connects over Ethernet, reads machine status, axis positions, alarms, program info, macro variables, tool offsets, and more. Supports both one-shot data dump and continuous monitoring modes.

## Features

- **One-shot mode** — dump all machine data (status, positions, alarms, programs, parameters, etc.)
- **Monitor mode** — continuously poll status, axis positions, and part counts
- **Batch collect mode** — read part counts from multiple machines defined in `jichuang.txt` and save to `result.txt`
- **Program upload** — display CNC program content

## Build

Requires Visual Studio Build Tools with x86 (32-bit) support. The FOCAS2 library is 32-bit, so the target must be x86.

```bat
build.bat
```

Output: `cnc_monitor.exe`, `cnc_collect.exe`

## Usage

```bat
cnc_monitor.exe <IP> [port] [options]
```

| Option | Description |
|--------|-------------|
| `<port>` | TCP port (default: 8193) |
| `-monitor [ms]` | Continuous monitoring (default interval: 1000ms) |
| `-parts <cur> <tot>` | Macro variables for part count (default: #500 #501) |
| `-show <prog>` | Display program content (e.g. `-show 1`) |

### Examples

```bat
cnc_monitor.exe 192.168.1.100
cnc_monitor.exe 192.168.1.100 8193 -monitor 500
cnc_monitor.exe 192.168.1.100 -parts 500 501 -monitor
cnc_collect.exe
```

## Project Structure

| File/Dir | Purpose |
|----------|---------|
| `main.c` | Interactive CNC monitor entry point |
| `collect.c` | Batch data collector entry point |
| `cnc_ops.h/c` | All CNC machine operations (FOCAS2 API calls) |
| `file_io.h/c` | Config file read / result file write |
| `third_party/fwlib/` | FANUC FOCAS2 library (header, DLL, import lib) |
| `jichuang.txt` | Machine list for batch collector |
| `build.bat` | MSVC build script |

## Dependencies

- **Fwlib32.dll** — FANUC FOCAS2 library (32-bit). Must be in the same directory as the executable or on `PATH`.
- **fwlib32.h** / **Fwlib32.lib** — from the [FOCAS2 SDK](https://github.com/strangesast/fwlib).

## License

MIT
