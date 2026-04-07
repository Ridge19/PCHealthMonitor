## Features

| Feature                    | Description                                                               |
| -------------------------- | ------------------------------------------------------------------------- |
| **CPU Monitoring**   | Total and per-core usage with live bar graphs, frequency, model info      |
| **Memory**           | RAM and swap usage with available/buffered/cached breakdown               |
| **Temperature**      | Reads from `/sys/class/thermal` and `/sys/class/hwmon` sensors        |
| **Disk Usage**       | All mounted drives with usage bars, filesystem type, space remaining      |
| **GPU Info**         | NVIDIA (via `nvidia-smi`), AMD, and Intel GPU detection                 |
| **Network**          | Interface status, link speed, MAC address, RX/TX traffic counters         |
| **Battery**          | Charge level, charging status, health (laptops)                           |
| **Top Processes**    | Top 5 CPU-consuming processes updated in real time                        |
| **Load Average**     | 1, 5, and 15-minute system load                                           |
| **In-Place Refresh** | Values update without flickering — no screen clearing                    |
| **Scrollable**       | Full keyboard scrolling with arrow keys, Page Up/Down, Home/End, vim keys |
| **Color Coded**      | Green/yellow/red thresholds so you can spot problems at a glance          |
| **Lightweight**      | Single compiled binary, no runtime dependencies, near-zero overhead       |
| **Native Linux**     | Reads directly from `/proc` and `/sys` — no external tools required  |

## Quick Start

### One-Line Install

```bash
curl -sL https://raw.githubusercontent.com/ridge/pchealth/main/scripts/quick-install.sh | bash
```
