#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

struct GpuDevice {
    std::wstring instanceId;
    std::wstring driverKey;
    std::wstring originalName;
    std::wstring currentName;
    std::wstring friendlyName;
    std::wstring registryPath;
    bool hasCustomName = false;
    int index = 0;
};

class GpuManager {
public:
    static std::vector<GpuDevice> EnumerateGpus();
    static bool SetFriendlyName(const GpuDevice& gpu, const std::wstring& name);
    static bool RemoveFriendlyName(const GpuDevice& gpu);
    static bool RefreshDevice(const GpuDevice& gpu);
    static bool RemoveLeftoverVirtualAdapter();
    static bool HasLeftoverVirtualAdapter();
    static std::optional<std::wstring> FindEnumPathByDriverKey(const std::wstring& driverKey);

private:
    static std::wstring ReadRegistryString(HKEY key, const wchar_t* valueName);
    static std::wstring InstanceIdToEnumPath(const std::wstring& instanceId);
    static bool KeepAdapter(const std::wstring& instanceId, const std::wstring& driverDesc);
    static bool IsLeftoverVirtual(const std::wstring& instanceId);
};
