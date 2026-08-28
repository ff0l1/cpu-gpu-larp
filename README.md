# larp

One app to change the **GPU** and **CPU** names Windows shows in Task Manager.

Pick a preset, search the list, or type your own. Run as Administrator. Restart Task Manager after applying.

## Build

Needs [CMake](https://cmake.org/) and Visual Studio with C++. Place the [ur](https://github.com/ff0l/ui-framework) UI library next to this folder as `ui Framework`, or set `UR_FRAMEWORK_DIR`.

```
build.bat
```

Run `build\Release\larp.exe` as Administrator.

## Use

Open **larp.exe** — it relaunches elevated if needed.

### GPU tab

1. If you have more than one physical GPU, pick the adapter.
2. Open a preset group or search (e.g. `4090`, `7900`).
3. Set the custom name if you want something not in the list.
4. Click **Apply**.
5. Restart Task Manager.

**Restore** removes the custom `FriendlyName` and brings back the driver default.

### CPU tab

1. Open a preset group or search (e.g. `9950X`, `Ultra 7`).
2. Set the custom name if needed.
3. Click **Apply** — all logical cores are updated together.
4. Restart Task Manager.

**Restore** removes the spoof and puts the original name back.

## How it works

### GPU

Windows stores each display adapter under:

`HKLM\SYSTEM\CurrentControlSet\Enum\<instance-id>`

larp writes a `FriendlyName` string value on that key, then sends a PnP property change so the new name is picked up without a reboot.

| File | Role |
|------|------|
| `src/gpu_manager.cpp` | Enumerates GPUs via SetupAPI, writes `FriendlyName`, refreshes the device |
| `src/gpu_presets.hpp` | NVIDIA / AMD / Intel preset names |

Virtual adapters (`ROOT\DISPLAY`, Microsoft Basic Display, etc.) are ignored.

### CPU

CPUs need two registry locations:

1. **Enum FriendlyName** — one key per logical core (`\0`, `\1`, …) under `HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\...`
2. **CentralProcessor** — `ProcessorNameString` under `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\{n}` — this is what Task Manager reads

larp writes both in one click and groups logical cores by physical package.

| File | Role |
|------|------|
| `src/cpu_manager.cpp` | Enumerates processors, writes all cores + CentralProcessor keys |
| `src/cpu_presets.hpp` | Intel / AMD preset names |

### UI

| File | Role |
|------|------|
| `src/main.cpp` | Single window with **GPU** and **CPU** tabs (`ur` framework) |
| `src/app.manifest` | Requires administrator |

## Links

ff0l — https://github.com/ff0l/larp
