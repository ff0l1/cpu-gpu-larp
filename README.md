# CPU-GPU-Larp

Change the **GPU** and **CPU** names shown in Windows Task Manager.

Pick a preset, search the list, or type your own name. The app relaunches as Administrator when needed. Restart Task Manager after applying.

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows-0078d4?style=flat-square" alt="Windows">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599c?style=flat-square" alt="C++20">
</p>

## Features

- **GPU** and **CPU** in one app
- Borderless overlay UI with preset search
- NVIDIA, AMD, Intel Arc, and fun presets
- **Apply** / **Restore** / **Refresh**
- Multi-GPU adapter picker

## Requirements

- Windows 10 or 11
- Administrator rights (registry changes)
- [CMake](https://cmake.org/) 3.20+
- Visual Studio 2022+ with C++ desktop development
- [ur UI framework](https://github.com/ff0l/ui-framework) — place it next to this repo as `ui Framework`, or set `UR_FRAMEWORK_DIR`

## Build

```bat
build.bat
```

Output: `build\Release\CPU-GPU-Larp.exe`

Run the executable as Administrator.

## Usage

### GPU

1. Open the **GPU** tab.
2. If multiple adapters are listed, pick the one you want.
3. Search or scroll presets, or edit **Custom name**.
4. Click **Apply**.
5. Restart Task Manager.

**Restore** removes the custom name and returns the driver default.

### CPU

1. Open the **CPU** tab.
2. Pick a preset or set a custom name.
3. Click **Apply** — all logical cores are updated together.
4. Restart Task Manager.

**Restore** removes the spoof and puts the original name back.

## How it works

### GPU

Task Manager reads the display adapter **FriendlyName** under:

`HKLM\SYSTEM\CurrentControlSet\Enum\<instance-id>`

The app writes that value and triggers a PnP property change so the new name shows up without a reboot. Virtual adapters (Basic Display, Remote Desktop, etc.) are ignored.

| File | Purpose |
|------|---------|
| `src/gpu_manager.cpp` | GPU enumeration, registry writes, device refresh |
| `src/gpu_presets.hpp` | Preset names |

### CPU

Task Manager reads **ProcessorNameString** under:

`HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\{n}`

The app also writes **FriendlyName** on each logical core under `Enum\ACPI\...` so everything stays in sync.

| File | Purpose |
|------|---------|
| `src/cpu_manager.cpp` | CPU enumeration, registry writes, refresh |
| `src/cpu_presets.hpp` | Preset names |

### UI

| File | Purpose |
|------|---------|
| `src/main.cpp` | Overlay window, tabs, preset lists |
| `src/app.manifest` | Administrator elevation |

## Project layout

```
CPU-GPU-Larp/
├── src/
│   ├── main.cpp
│   ├── gpu_manager.cpp / .hpp
│   ├── cpu_manager.cpp / .hpp
│   ├── gpu_presets.hpp
│   ├── cpu_presets.hpp
│   └── app.manifest
├── CMakeLists.txt
├── build.bat
└── README.md
```

## Author

[ff0l](https://github.com/ff0l)
