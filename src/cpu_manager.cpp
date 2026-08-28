#include "cpu_manager.hpp"

#define INITGUID
#include <windows.h>
#include <setupapi.h>
#include <devpkey.h>
#include <devguid.h>

#include <algorithm>
#include <map>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace {

std::map<std::wstring, std::wstring> g_baselineSystemNames;

std::wstring GetDevicePropertyString(HDEVINFO devInfo, PSP_DEVINFO_DATA devInfoData, const DEVPROPKEY& key) {
    DEVPROPTYPE propType = DEVPROP_TYPE_NULL;
    ULONG required = 0;

    if (!SetupDiGetDevicePropertyW(devInfo, devInfoData, &key, &propType, nullptr, 0, &required, 0) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return {};
    }

    if (required == 0) {
        return {};
    }

    std::vector<BYTE> buffer(required);
    if (!SetupDiGetDevicePropertyW(devInfo, devInfoData, &key, &propType, buffer.data(), required, &required, 0)) {
        return {};
    }

    if (propType != DEVPROP_TYPE_STRING) {
        return {};
    }

    return reinterpret_cast<const wchar_t*>(buffer.data());
}

std::wstring TrimCopy(std::wstring value) {
    auto notSpace = [](wchar_t ch) { return !iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring StripInfReference(const std::wstring& value) {
    std::wstring trimmed = TrimCopy(value);
    if (trimmed.empty()) {
        return trimmed;
    }

    while (!trimmed.empty() && (trimmed.front() == L'\uFEFF' || trimmed.front() == L'\uFFFD')) {
        trimmed.erase(trimmed.begin());
    }

    if (trimmed.starts_with(L'@') || trimmed.starts_with(L'%')) {
        const auto semi = trimmed.rfind(L';');
        if (semi != std::wstring::npos && semi + 1 < trimmed.size()) {
            trimmed = TrimCopy(trimmed.substr(semi + 1));
        }
    }

    while (!trimmed.empty() && (trimmed.front() == L'\uFEFF' || trimmed.front() == L'\uFFFD')) {
        trimmed.erase(trimmed.begin());
    }

    return TrimCopy(trimmed);
}

}

std::wstring CpuManager::ReadRegistryString(HKEY key, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD size = 0;

    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS) {
        return {};
    }

    return TrimCopy(buffer.data());
}

std::wstring CpuManager::InstanceIdToEnumPath(const std::wstring& instanceId) {
    return L"SYSTEM\\CurrentControlSet\\Enum\\" + instanceId;
}

std::wstring CpuManager::PackageKeyFromInstanceId(const std::wstring& instanceId) {
    const auto pos = instanceId.find_last_of(L'\\');
    if (pos == std::wstring::npos) {
        return instanceId;
    }

    const std::wstring suffix = instanceId.substr(pos + 1);
    if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), iswdigit)) {
        return instanceId.substr(0, pos);
    }

    return instanceId;
}

bool CpuManager::WriteFriendlyNameAtPath(const std::wstring& enumPath, const std::wstring& name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, enumPath.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG status = RegSetValueExW(
        key,
        L"FriendlyName",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(name.c_str()),
        static_cast<DWORD>((name.size() + 1) * sizeof(wchar_t)));

    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool CpuManager::DeleteFriendlyNameAtPath(const std::wstring& enumPath) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, enumPath.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG status = RegDeleteValueW(key, L"FriendlyName");
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

std::wstring CpuManager::ReadSystemProcessorName() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0,
            KEY_READ | KEY_WOW64_64KEY,
            &key) != ERROR_SUCCESS) {
        return {};
    }

    const std::wstring value = StripInfReference(ReadRegistryString(key, L"ProcessorNameString"));
    RegCloseKey(key);
    return value;
}

bool CpuManager::WriteSystemProcessorName(const std::wstring& name, int logicalCores) {
    const std::wstring sanitized = StripInfReference(name);
    if (sanitized.empty() || logicalCores <= 0) {
        return false;
    }

    bool any = false;
    for (int i = 0; i < logicalCores; ++i) {
        const std::wstring subkey = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\" + std::to_wstring(i);
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
            continue;
        }

        const LONG status = RegSetValueExW(
            key,
            L"ProcessorNameString",
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(sanitized.c_str()),
            static_cast<DWORD>((sanitized.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        any = status == ERROR_SUCCESS || any;
    }

    return any;
}

bool CpuManager::RefreshInstance(const std::wstring& instanceId) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_PROCESSOR, instanceId.c_str(), nullptr, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    bool refreshed = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &data); ++i) {
        wchar_t currentId[512]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &data, currentId, static_cast<DWORD>(std::size(currentId)), nullptr)) {
            continue;
        }
        if (_wcsicmp(currentId, instanceId.c_str()) != 0) {
            continue;
        }

        SP_PROPCHANGE_PARAMS change{};
        change.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        change.StateChange = DICS_PROPCHANGE;
        change.Scope = DICS_FLAG_GLOBAL;
        change.HwProfile = 0;

        if (SetupDiSetClassInstallParamsW(devInfo, &data, &change.ClassInstallHeader, sizeof(change))) {
            refreshed = SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devInfo, &data) != FALSE;
        }
        break;
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return refreshed;
}

std::vector<CpuDevice> CpuManager::EnumerateCpus() {
    std::map<std::wstring, CpuDevice> packages;

    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_PROCESSOR, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return {};
    }

    SP_DEVINFO_DATA devInfoData{};
    devInfoData.cbSize = sizeof(devInfoData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); ++i) {
        wchar_t instanceId[512]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &devInfoData, instanceId, static_cast<DWORD>(std::size(instanceId)), nullptr)) {
            continue;
        }

        const std::wstring packageKey = PackageKeyFromInstanceId(instanceId);
        const std::wstring registryPath = InstanceIdToEnumPath(instanceId);
        const std::wstring driverDesc = StripInfReference(GetDevicePropertyString(devInfo, &devInfoData, DEVPKEY_Device_DriverDesc));
        const std::wstring driverKey = GetDevicePropertyString(devInfo, &devInfoData, DEVPKEY_Device_Driver);

        auto& cpu = packages[packageKey];
        if (cpu.packageKey.empty()) {
            cpu.packageKey = packageKey;
            cpu.driverKey = driverKey;
            cpu.originalName = driverDesc;
            cpu.systemName = ReadSystemProcessorName();
        }

        cpu.instanceIds.push_back(instanceId);
        cpu.registryPaths.push_back(registryPath);
        ++cpu.logicalCores;

        HKEY enumKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, registryPath.c_str(), 0, KEY_READ, &enumKey) == ERROR_SUCCESS) {
            const std::wstring friendly = TrimCopy(StripInfReference(ReadRegistryString(enumKey, L"FriendlyName")));
            if (!friendly.empty()) {
                cpu.friendlyName = friendly;
                cpu.hasCustomName = true;
            }
            RegCloseKey(enumKey);
        }

        if (cpu.friendlyName.empty()) {
            std::wstring name = GetDevicePropertyString(devInfo, &devInfoData, DEVPKEY_Device_FriendlyName);
            if (name.empty()) {
                name = driverDesc;
            }
            cpu.friendlyName = TrimCopy(StripInfReference(name));
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);

    std::vector<CpuDevice> cpus;
    for (auto& [_, cpu] : packages) {
        if (cpu.originalName.empty()) {
            cpu.originalName = cpu.friendlyName;
        }
        cpu.currentName = cpu.friendlyName;
        cpus.push_back(std::move(cpu));
    }

    std::sort(cpus.begin(), cpus.end(), [](const CpuDevice& a, const CpuDevice& b) {
        return a.packageKey < b.packageKey;
    });

    return cpus;
}

bool CpuManager::RefreshPackage(const CpuDevice& cpu) {
    bool any = false;
    for (const auto& instanceId : cpu.instanceIds) {
        any = RefreshInstance(instanceId) || any;
    }
    return any;
}

bool CpuManager::SetFriendlyName(const CpuDevice& cpu, const std::wstring& name) {
    const std::wstring sanitized = StripInfReference(name);
    if (sanitized.empty() || cpu.registryPaths.empty()) {
        return false;
    }

    if (g_baselineSystemNames.find(cpu.packageKey) == g_baselineSystemNames.end()) {
        const std::wstring current = ReadSystemProcessorName();
        if (!current.empty() && _wcsicmp(current.c_str(), sanitized.c_str()) != 0) {
            g_baselineSystemNames[cpu.packageKey] = current;
        }
    }

    bool any = false;
    for (const auto& path : cpu.registryPaths) {
        any = WriteFriendlyNameAtPath(path, sanitized) || any;
    }

    if (any) {
        WriteSystemProcessorName(sanitized, cpu.logicalCores);
        RefreshPackage(cpu);
    }
    return any;
}

bool CpuManager::RemoveFriendlyName(const CpuDevice& cpu) {
    if (cpu.registryPaths.empty()) {
        return false;
    }

    bool any = false;
    for (const auto& path : cpu.registryPaths) {
        any = DeleteFriendlyNameAtPath(path) || any;
    }

    if (any) {
        const auto baseline = g_baselineSystemNames.find(cpu.packageKey);
        const std::wstring restoreName = baseline != g_baselineSystemNames.end()
            ? baseline->second
            : (!cpu.systemName.empty() ? cpu.systemName : cpu.originalName);
        WriteSystemProcessorName(restoreName, cpu.logicalCores);
        RefreshPackage(cpu);
    }
    return any;
}
