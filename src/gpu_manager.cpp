#include "gpu_manager.hpp"

#define INITGUID
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <devguid.h>

#include <algorithm>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace {

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

std::wstring LowerCopy(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

}

std::wstring GpuManager::ReadRegistryString(HKEY key, const wchar_t* valueName) {
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

std::wstring GpuManager::InstanceIdToEnumPath(const std::wstring& instanceId) {
    return L"SYSTEM\\CurrentControlSet\\Enum\\" + instanceId;
}

bool GpuManager::IsLeftoverVirtual(const std::wstring& instanceId) {
    const std::wstring lower = LowerCopy(instanceId);
    return lower.starts_with(L"root\\display") || lower.find(L"larpgpu") != std::wstring::npos;
}

bool GpuManager::KeepAdapter(const std::wstring& instanceId, const std::wstring& driverDesc) {
    if (IsLeftoverVirtual(instanceId)) {
        return false;
    }
    if (instanceId.starts_with(L"ROOT\\") || instanceId.starts_with(L"SWD\\")) {
        return false;
    }

    const std::wstring lowerDesc = LowerCopy(driverDesc);
    if (lowerDesc.find(L"microsoft basic display") != std::wstring::npos) {
        return false;
    }
    if (lowerDesc.find(L"remote desktop") != std::wstring::npos) {
        return false;
    }
    if (lowerDesc.find(L"virtual display") != std::wstring::npos) {
        return false;
    }
    if (lowerDesc.find(L"larp-gpu adapter") != std::wstring::npos) {
        return false;
    }
    return true;
}

std::optional<std::wstring> GpuManager::FindEnumPathByDriverKey(const std::wstring& driverKey) {
    if (driverKey.empty()) {
        return std::nullopt;
    }

    HKEY enumKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum", 0, KEY_READ, &enumKey) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    std::optional<std::wstring> result;

    auto searchSubkeys = [&](auto&& self, HKEY parent, const std::wstring& pathPrefix) -> void {
        if (result.has_value()) {
            return;
        }

        DWORD index = 0;
        wchar_t subkeyName[512];
        DWORD subkeyLen = static_cast<DWORD>(std::size(subkeyName));

        while (RegEnumKeyExW(parent, index++, subkeyName, &subkeyLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            HKEY child = nullptr;
            if (RegOpenKeyExW(parent, subkeyName, 0, KEY_READ, &child) != ERROR_SUCCESS) {
                subkeyLen = static_cast<DWORD>(std::size(subkeyName));
                continue;
            }

            const std::wstring currentPath = pathPrefix.empty() ? subkeyName : pathPrefix + L"\\" + subkeyName;
            const std::wstring driverValue = ReadRegistryString(child, L"Driver");

            if (!driverValue.empty() && _wcsicmp(driverValue.c_str(), driverKey.c_str()) == 0) {
                result = L"SYSTEM\\CurrentControlSet\\Enum\\" + currentPath;
                RegCloseKey(child);
                return;
            }

            self(self, child, currentPath);
            RegCloseKey(child);

            if (result.has_value()) {
                return;
            }

            subkeyLen = static_cast<DWORD>(std::size(subkeyName));
        }
    };

    searchSubkeys(searchSubkeys, enumKey, L"");
    RegCloseKey(enumKey);
    return result;
}

std::vector<GpuDevice> GpuManager::EnumerateGpus() {
    std::vector<GpuDevice> gpus;

    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return gpus;
    }

    SP_DEVINFO_DATA devInfoData{};
    devInfoData.cbSize = sizeof(devInfoData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devInfoData); ++i) {
        wchar_t instanceId[512]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &devInfoData, instanceId, static_cast<DWORD>(std::size(instanceId)), nullptr)) {
            continue;
        }

        const std::wstring driverDesc = StripInfReference(GetDevicePropertyString(devInfo, &devInfoData, DEVPKEY_Device_DriverDesc));
        if (!KeepAdapter(instanceId, driverDesc)) {
            continue;
        }

        GpuDevice gpu{};
        gpu.instanceId = instanceId;
        gpu.driverKey = GetDevicePropertyString(devInfo, &devInfoData, DEVPKEY_Device_Driver);
        gpu.registryPath = InstanceIdToEnumPath(gpu.instanceId);

        HKEY enumKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, gpu.registryPath.c_str(), 0, KEY_READ, &enumKey) == ERROR_SUCCESS) {
            const std::wstring friendly = ReadRegistryString(enumKey, L"FriendlyName");
            if (!friendly.empty()) {
                gpu.friendlyName = StripInfReference(friendly);
                gpu.hasCustomName = true;
            }
            RegCloseKey(enumKey);
        }

        if (gpu.friendlyName.empty()) {
            std::wstring name = GetDevicePropertyString(devInfo, &devInfoData, DEVPKEY_Device_FriendlyName);
            if (name.empty()) {
                name = driverDesc;
            }
            gpu.friendlyName = StripInfReference(name);
        }

        gpu.originalName = driverDesc.empty() ? gpu.friendlyName : driverDesc;
        gpu.currentName = gpu.friendlyName;
        gpu.index = static_cast<int>(gpus.size());
        gpus.push_back(std::move(gpu));
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return gpus;
}

bool GpuManager::SetFriendlyName(const GpuDevice& gpu, const std::wstring& name) {
    const std::wstring sanitized = StripInfReference(name);
    if (sanitized.empty()) {
        return false;
    }

    std::wstring enumPath = gpu.registryPath;
    if (enumPath.empty()) {
        const auto found = FindEnumPathByDriverKey(gpu.driverKey);
        if (!found.has_value()) {
            return false;
        }
        enumPath = found.value();
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, enumPath.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG status = RegSetValueExW(
        key,
        L"FriendlyName",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(sanitized.c_str()),
        static_cast<DWORD>((sanitized.size() + 1) * sizeof(wchar_t)));

    RegCloseKey(key);

    if (status != ERROR_SUCCESS) {
        return false;
    }

    RefreshDevice(gpu);
    return true;
}

bool GpuManager::RemoveFriendlyName(const GpuDevice& gpu) {
    std::wstring enumPath = gpu.registryPath;
    if (enumPath.empty()) {
        const auto found = FindEnumPathByDriverKey(gpu.driverKey);
        if (!found.has_value()) {
            return false;
        }
        enumPath = found.value();
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, enumPath.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }

    const LONG status = RegDeleteValueW(key, L"FriendlyName");
    RegCloseKey(key);

    const bool ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    if (ok) {
        RefreshDevice(gpu);
    }
    return ok;
}

bool GpuManager::RefreshDevice(const GpuDevice& gpu) {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, gpu.instanceId.c_str(), nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    }
    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    bool refreshed = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &data); ++i) {
        wchar_t instanceId[512]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &data, instanceId, static_cast<DWORD>(std::size(instanceId)), nullptr)) {
            continue;
        }
        if (_wcsicmp(instanceId, gpu.instanceId.c_str()) != 0) {
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

bool GpuManager::HasLeftoverVirtualAdapter() {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    bool found = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &data); ++i) {
        wchar_t instanceId[512]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &data, instanceId, static_cast<DWORD>(std::size(instanceId)), nullptr)) {
            continue;
        }
        if (IsLeftoverVirtual(instanceId)) {
            found = true;
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}

bool GpuManager::RemoveLeftoverVirtualAdapter() {
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, 0);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    bool removed = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &data); ++i) {
        wchar_t instanceId[512]{};
        if (!SetupDiGetDeviceInstanceIdW(devInfo, &data, instanceId, static_cast<DWORD>(std::size(instanceId)), nullptr)) {
            continue;
        }
        if (!IsLeftoverVirtual(instanceId)) {
            continue;
        }

        SP_REMOVEDEVICE_PARAMS remove{};
        remove.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        remove.ClassInstallHeader.InstallFunction = DIF_REMOVE;
        remove.Scope = DI_REMOVEDEVICE_GLOBAL;
        remove.HwProfile = 0;

        if (SetupDiSetClassInstallParamsW(devInfo, &data, &remove.ClassInstallHeader, sizeof(remove))) {
            removed = SetupDiCallClassInstaller(DIF_REMOVE, devInfo, &data) || removed;
        }
        if (!removed) {
            removed = SetupDiRemoveDevice(devInfo, &data) != FALSE || removed;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return removed;
}
