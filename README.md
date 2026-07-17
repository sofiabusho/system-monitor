# System Monitor

Linux desktop monitor built on Dear ImGui + SDL2. Reads live data from `/proc` and `/sys` and shows it in three windows: **System**, **Memory and Processes**, and **Network**.

## Build

Dependencies (Debian/Ubuntu):

```bash
sudo apt-get install build-essential libsdl2-dev
```

Then:

```bash
make
./monitor
```

Clean with `make clean`.

## What it shows

**System**
- OS name, logged-in user, hostname, CPU model
- Task counts by state (running, sleeping, uninterruptible, zombie, stopped, idle)
- Tabs for **CPU**, **Fan**, and **Thermal** with live graphs, pause, FPS, and Y-scale controls
- Fan/Thermal prefer ThinkPad `/proc/acpi/ibm/*` paths when present; otherwise scan `hwmon`. Missing sensors are reported instead of crashing.

**Memory and Processes**
- RAM, SWAP, and root disk usage bars
- Process table: PID, Name, State, CPU %, Memory %
- Text filter and multi-row selection

**Network**
- IPv4 addresses per interface
- RX / TX counter tables from `/proc/net/dev`
- RX / TX usage bars with automatic KB/MB/GB labels on a 0–2 GB scale

## Layout

| File | Role |
|------|------|
| `main.cpp` | ImGui windows and main loop |
| `system.cpp` | System / CPU / fan / thermal collectors |
| `mem.cpp` | Memory, disk, process list |
| `network.cpp` | IPv4 and interface counters |
| `header.h` | Shared types and declarations |
| `imgui/` | Dear ImGui + SDL/OpenGL backend (starter) |

## Notes

- Values refresh while the window runs; process CPU % needs at least two samples.
- Fan and thermal hardware varies by machine — especially under WSL or VMs.
