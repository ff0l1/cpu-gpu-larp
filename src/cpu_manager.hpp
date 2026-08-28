#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct CpuDevice {
    std::wstring packageKey;
    std::wstring driverKey;
    std::wstring originalName;
    std::wstring systemName;
    std::wstring currentName;
    std::wstring friendlyName;
    std::vector<std::wstring> instanceIds;
    std::vector<std::wstring> registryPaths;
    bool hasCustomName = false;
    int logicalCores = 0;
};

class CpuManager {
public:
    static std::vector<CpuDevice> EnumerateCpus();
    static bool SetFriendlyName(const CpuDevice& cpu, const std::wstring& name);
    static bool RemoveFriendlyName(const CpuDevice& cpu);
    static bool RefreshPackage(const CpuDevice& cpu);

private:
    static std::wstring ReadRegistryString(HKEY key, const wchar_t* valueName);
    static std::wstring InstanceIdToEnumPath(const std::wstring& instanceId);
    static std::wstring PackageKeyFromInstanceId(const std::wstring& instanceId);
    static bool WriteFriendlyNameAtPath(const std::wstring& enumPath, const std::wstring& name);
    static bool DeleteFriendlyNameAtPath(const std::wstring& enumPath);
    static std::wstring ReadSystemProcessorName();
    static bool WriteSystemProcessorName(const std::wstring& name, int logicalCores);
    static bool RefreshInstance(const std::wstring& instanceId);
};
