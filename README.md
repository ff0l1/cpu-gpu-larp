# Task Manager names

Changes the CPU and GPU names Windows Task Manager shows. Pick a preset, search the list, or type your own. The app asks for Administrator when it needs the registry. Restart Task Manager after Apply.

## Use

GPU tab: pick the adapter if you have more than one, then Apply. Restore puts the driver name back.

CPU tab: one name is written to every logical core. Restore puts the old string back.

Virtual adapters (Basic Display, Remote Desktop, and similar) are skipped.

## Build

Windows 10 or 11, CMake 3.20+, Visual Studio 2022 with the C++ desktop workload. The UI comes from [ur](https://github.com/ff0l1/ur). Put that checkout next to this repo as `ui Framework`, or set `UR_FRAMEWORK_DIR`.

```bat
build.bat
```

Output: `build\Release\CPU-GPU-Larp.exe`

## How the names stick

Task Manager reads the GPU **FriendlyName** under `HKLM\SYSTEM\CurrentControlSet\Enum\<instance>`. The app writes that value and pokes PnP so you do not need a reboot.

The CPU name is **ProcessorNameString** under `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\{n}`, plus **FriendlyName** on the ACPI enum nodes so the two stay in sync.

## Files

```
src/main.cxx            window, tabs, lists
src/gpu_manager.cxx     adapters and registry
src/cpu_manager.cxx     cores and registry
src/gpu_presets.hxx
src/cpu_presets.hxx
src/app.manifest        elevation
```
