#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <shellapi.h>

#include "ur/ur.hpp"
#include "gpu_manager.hpp"
#include "cpu_manager.hpp"
#include "gpu_presets.hpp"
#include "cpu_presets.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class Page {
    Gpu,
    Cpu,
};

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 1) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

Page g_page = Page::Gpu;
bool g_initialized = false;
bool g_open = true;

std::vector<GpuDevice> g_gpus;
int g_selectedGpu = 0;
std::string g_gpuCustomName;
int g_gpuPresetSelected = -1;
char g_gpuSearch[128]{};
bool g_hasLeftover = false;
std::vector<std::string> g_gpuPresetNames;
std::vector<const char*> g_gpuPresetPtrs;
std::vector<std::string> g_gpuLabels;
std::vector<const char*> g_gpuLabelPtrs;
std::vector<bool> g_gpuGroupOpen;

std::vector<CpuDevice> g_cpus;
int g_selectedCpu = 0;
std::string g_cpuCustomName;
int g_cpuPresetSelected = -1;
char g_cpuSearch[128]{};
std::vector<std::string> g_cpuPresetNames;
std::vector<const char*> g_cpuPresetPtrs;
std::vector<bool> g_cpuGroupOpen;

void BuildFlatPresets(
    const auto& groups,
    std::vector<std::string>& names,
    std::vector<const char*>& ptrs) {
    names.clear();
    ptrs.clear();
    for (const auto& group : groups) {
        for (const auto& model : group.models) {
            names.emplace_back(WideToUtf8(model));
            ptrs.push_back(names.back().c_str());
        }
    }
}

void SyncPresetSelection(const std::string& customName, std::vector<std::string>& names, int& selected) {
    selected = -1;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == customName) {
            selected = static_cast<int>(i);
            break;
        }
    }
}

void UpdateGpuLabels() {
    g_gpuLabels.clear();
    g_gpuLabelPtrs.clear();
    for (size_t i = 0; i < g_gpus.size(); ++i) {
        g_gpuLabels.push_back("GPU " + std::to_string(i + 1) + " — " + WideToUtf8(g_gpus[i].currentName));
        g_gpuLabelPtrs.push_back(g_gpuLabels.back().c_str());
    }
}

void RefreshGpus() {
    g_gpus = GpuManager::EnumerateGpus();
    g_hasLeftover = GpuManager::HasLeftoverVirtualAdapter();
    if (g_gpus.empty()) {
        g_selectedGpu = 0;
        g_gpuCustomName.clear();
        g_gpuPresetSelected = -1;
        return;
    }
    g_selectedGpu = (std::min)(g_selectedGpu, static_cast<int>(g_gpus.size()) - 1);
    UpdateGpuLabels();
    g_gpuCustomName = WideToUtf8(g_gpus[g_selectedGpu].currentName);
    SyncPresetSelection(g_gpuCustomName, g_gpuPresetNames, g_gpuPresetSelected);
}

void RefreshCpus() {
    g_cpus = CpuManager::EnumerateCpus();
    if (g_cpus.empty()) {
        g_selectedCpu = 0;
        g_cpuCustomName.clear();
        g_cpuPresetSelected = -1;
        return;
    }
    g_selectedCpu = (std::min)(g_selectedCpu, static_cast<int>(g_cpus.size()) - 1);
    g_cpuCustomName = WideToUtf8(g_cpus[g_selectedCpu].currentName);
    SyncPresetSelection(g_cpuCustomName, g_cpuPresetNames, g_cpuPresetSelected);
}

void SelectGpu(int index) {
    if (index < 0 || index >= static_cast<int>(g_gpus.size())) {
        return;
    }
    g_selectedGpu = index;
    g_gpuCustomName = WideToUtf8(g_gpus[g_selectedGpu].currentName);
    SyncPresetSelection(g_gpuCustomName, g_gpuPresetNames, g_gpuPresetSelected);
}

void OnGpuApply() {
    if (g_gpus.empty()) {
        return;
    }
    const std::wstring name = Utf8ToWide(g_gpuCustomName);
    if (name.empty()) {
        ur::ui::notice("Pick a GPU name from the list or type one.");
        return;
    }
    if (!GpuManager::SetFriendlyName(g_gpus[g_selectedGpu], name)) {
        ur::ui::notice("Could not write the GPU name.");
        return;
    }
    RefreshGpus();
    ur::ui::notice("GPU name applied. Restart Task Manager.");
}

void OnGpuRestore() {
    if (g_gpus.empty()) {
        return;
    }
    if (!GpuManager::RemoveFriendlyName(g_gpus[g_selectedGpu])) {
        ur::ui::notice("Could not restore the GPU name.");
        return;
    }
    RefreshGpus();
    ur::ui::notice("GPU name restored.");
}

void OnGpuCleanupLeftover() {
    if (!GpuManager::RemoveLeftoverVirtualAdapter()) {
        ur::ui::notice("No leftover virtual adapter removed.");
        return;
    }
    RefreshGpus();
    ur::ui::notice("Removed leftover virtual adapter.");
}

void OnCpuApply() {
    if (g_cpus.empty()) {
        return;
    }
    const std::wstring name = Utf8ToWide(g_cpuCustomName);
    if (name.empty()) {
        ur::ui::notice("Pick a CPU name from the list or type one.");
        return;
    }
    if (!CpuManager::SetFriendlyName(g_cpus[g_selectedCpu], name)) {
        ur::ui::notice("Could not write the CPU name.");
        return;
    }
    RefreshCpus();
    ur::ui::notice("CPU name applied. Restart Task Manager.");
}

void OnCpuRestore() {
    if (g_cpus.empty()) {
        return;
    }
    if (!CpuManager::RemoveFriendlyName(g_cpus[g_selectedCpu])) {
        ur::ui::notice("Could not restore the CPU name.");
        return;
    }
    RefreshCpus();
    ur::ui::notice("CPU name restored.");
}

void DrawPresetGroups(
    const auto& groups,
    std::vector<bool>& groupOpen,
    std::string& customName,
    int& presetSelected,
    std::vector<std::string>& flatNames) {
    if (groupOpen.size() != groups.size()) {
        groupOpen.assign(groups.size(), false);
        if (!groupOpen.empty()) {
            groupOpen.front() = true;
        }
    }

    for (size_t i = 0; i < groups.size(); ++i) {
        const std::string vendor = WideToUtf8(groups[i].vendor);
        if (!Widgets->Collapsing(vendor.c_str(), groupOpen[i])) {
            continue;
        }

        std::vector<std::string> localNames;
        std::vector<const char*> localPtrs;
        localNames.reserve(groups[i].models.size());
        localPtrs.reserve(groups[i].models.size());
        for (const auto& model : groups[i].models) {
            localNames.emplace_back(WideToUtf8(model));
            localPtrs.push_back(localNames.back().c_str());
        }

        int localSelected = -1;
        for (size_t j = 0; j < localNames.size(); ++j) {
            if (localNames[j] == customName) {
                localSelected = static_cast<int>(j);
                break;
            }
        }

        if (Widgets->List(
                "##preset",
                localSelected,
                localPtrs.data(),
                static_cast<int>(localPtrs.size()),
                5)) {
            if (localSelected >= 0 && localSelected < static_cast<int>(localNames.size())) {
                customName = localNames[localSelected];
                SyncPresetSelection(customName, flatNames, presetSelected);
            }
        }
        Layout->Skip(4.0f);
    }
}

void DrawDeviceStatus(const char* emptyMessage, const std::string& currentName, const char* originalLine, const char* detailLine) {
    if (currentName.empty() && emptyMessage) {
        Widgets->Colored(Style->Warning, emptyMessage);
        ur::ui::faint("Run as Administrator.");
        return;
    }

    ur::ui::faint("Showing as");
    Widgets->Heading(currentName.c_str());
    if (originalLine) {
        ur::ui::faint(originalLine);
    }
    if (detailLine) {
        ur::ui::faint(detailLine);
    }
}

void DrawActionRow(bool enabled, auto&& onApply, auto&& onRestore, auto&& onRefresh) {
    ur::ui::disabled_scope guard(!enabled);
    if (ur::ui::button("Apply", 100.0f)) {
        onApply();
    }
    Layout->SameLine();
    if (ur::ui::button("Restore", 100.0f)) {
        onRestore();
    }
    Layout->SameLine();
    if (ur::ui::button("Refresh", 90.0f)) {
        onRefresh();
    }
}

void DrawGpuPage() {
    ur::ui::faint("Rename your GPU in Task Manager and Device Manager.");

    Layout->Skip(8.0f);

    if (g_gpus.size() > 1) {
        if (Widgets->Choice("Adapter", g_selectedGpu, g_gpuLabelPtrs.data(), static_cast<int>(g_gpuLabelPtrs.size()))) {
            SelectGpu(g_selectedGpu);
        }
        Layout->Skip(6.0f);
    }

    std::string originalLine;
    if (!g_gpus.empty() && g_gpus[g_selectedGpu].hasCustomName) {
        originalLine = "Original: " + WideToUtf8(g_gpus[g_selectedGpu].originalName);
    }
    DrawDeviceStatus(
        "No GPU detected",
        g_gpus.empty() ? std::string{} : WideToUtf8(g_gpus[g_selectedGpu].currentName),
        originalLine.empty() ? nullptr : originalLine.c_str(),
        nullptr);

    if (g_hasLeftover) {
        Layout->Skip(6.0f);
        Widgets->Colored(Style->Warning, "Old virtual adapter detected");
        Layout->SameLine();
        if (ur::ui::button("Remove", 80.0f)) {
            OnGpuCleanupLeftover();
        }
    }

    Layout->Skip(10.0f);

    if (*g_gpuSearch != '\0') {
        if (Widgets->FilterList(
                "Search presets",
                g_gpuPresetSelected,
                g_gpuPresetPtrs.data(),
                static_cast<int>(g_gpuPresetPtrs.size()),
                g_gpuSearch,
                static_cast<int>(sizeof(g_gpuSearch)),
                7)) {
            if (g_gpuPresetSelected >= 0 && g_gpuPresetSelected < static_cast<int>(g_gpuPresetNames.size())) {
                g_gpuCustomName = g_gpuPresetNames[g_gpuPresetSelected];
            }
        }
    } else {
        DrawPresetGroups(GetGpuPresets(), g_gpuGroupOpen, g_gpuCustomName, g_gpuPresetSelected, g_gpuPresetNames);
    }

    Layout->Skip(8.0f);
    ur::ui::field("Search presets", g_gpuSearch, static_cast<int>(sizeof(g_gpuSearch)), "4090, 7900, Arc...");
    Layout->Skip(4.0f);
    ur::ui::field("Custom name", g_gpuCustomName, "NVIDIA GeForce RTX 4090");

    Layout->Skip(8.0f);
    DrawActionRow(!g_gpus.empty(), OnGpuApply, OnGpuRestore, RefreshGpus);
}

void DrawCpuPage() {
    ur::ui::faint("Rename your CPU in Task Manager. All logical cores update together.");

    Layout->Skip(8.0f);

    std::string originalLine;
    std::string detailLine;
    if (!g_cpus.empty()) {
        const CpuDevice& cpu = g_cpus[g_selectedCpu];
        if (cpu.hasCustomName) {
            originalLine = "Original: " + WideToUtf8(cpu.originalName);
        }
        detailLine = std::to_string(cpu.logicalCores) + " logical cores";
    }

    DrawDeviceStatus(
        "No CPU detected",
        g_cpus.empty() ? std::string{} : WideToUtf8(g_cpus[g_selectedCpu].currentName),
        originalLine.empty() ? nullptr : originalLine.c_str(),
        detailLine.empty() ? nullptr : detailLine.c_str());

    Layout->Skip(10.0f);

    if (*g_cpuSearch != '\0') {
        if (Widgets->FilterList(
                "Search presets",
                g_cpuPresetSelected,
                g_cpuPresetPtrs.data(),
                static_cast<int>(g_cpuPresetPtrs.size()),
                g_cpuSearch,
                static_cast<int>(sizeof(g_cpuSearch)),
                7)) {
            if (g_cpuPresetSelected >= 0 && g_cpuPresetSelected < static_cast<int>(g_cpuPresetNames.size())) {
                g_cpuCustomName = g_cpuPresetNames[g_cpuPresetSelected];
            }
        }
    } else {
        DrawPresetGroups(GetCpuPresets(), g_cpuGroupOpen, g_cpuCustomName, g_cpuPresetSelected, g_cpuPresetNames);
    }

    Layout->Skip(8.0f);
    ur::ui::field("Search presets", g_cpuSearch, static_cast<int>(sizeof(g_cpuSearch)), "9950X, Ultra 7, Potato...");
    Layout->Skip(4.0f);
    ur::ui::field("Custom name", g_cpuCustomName, "Intel(R) Core(TM) Ultra 7 265K");

    Layout->Skip(8.0f);
    DrawActionRow(!g_cpus.empty(), OnCpuApply, OnCpuRestore, RefreshCpus);
}

void DrawUi() {
    const float width = static_cast<float>(ur::app::width());
    const float height = static_cast<float>(ur::app::height());

    if (ur::ui::window panel("##host", &g_open, FramePin | FrameClose, CVector(0.0f, 0.0f), CVector(width, height)); panel) {
        Widgets->Heading("larp");
        ur::ui::faint("Spoof GPU and CPU names shown in Task Manager.");

        Layout->Skip(8.0f);

        if (Widgets->BeginTabs("##pages")) {
            if (Widgets->Tab("GPU")) {
                g_page = Page::Gpu;
            }
            if (Widgets->Tab("CPU")) {
                g_page = Page::Cpu;
            }
            Widgets->EndTabs();
        }

        Layout->Skip(10.0f);

        if (g_page == Page::Gpu) {
            DrawGpuPage();
        } else {
            DrawCpuPage();
        }

        Layout->Skip(10.0f);
        ur::ui::faint("github.com/ff0l/larp");
    }

    if (!g_open) {
        ur::app::quit();
    }
}

bool IsRunningAsAdmin() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        elevated = elevation.TokenIsElevated;
    }
    CloseHandle(token);
    return elevated == TRUE;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!IsRunningAsAdmin()) {
        MessageBoxW(nullptr, L"larp needs Administrator to change device names.", L"larp", MB_ICONWARNING | MB_OK);
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        ShellExecuteW(nullptr, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    ur::app::Config config;
    config.title = "larp";
    config.width = 560;
    config.height = 640;
    config.backend = ur::Backend::Auto;
    config.vsync = true;
    config.docking = false;
    config.persist = false;
    config.media = false;
    config.hear = false;
    config.discord = false;
    config.overlay = false;

    return ur::app::run(config, [] {
        if (!g_initialized) {
            ur::theme::apply(7);
            Style->Glass = false;
            BuildFlatPresets(GetGpuPresets(), g_gpuPresetNames, g_gpuPresetPtrs);
            BuildFlatPresets(GetCpuPresets(), g_cpuPresetNames, g_cpuPresetPtrs);
            g_gpuGroupOpen.assign(GetGpuPresets().size(), false);
            g_cpuGroupOpen.assign(GetCpuPresets().size(), false);
            if (!g_gpuGroupOpen.empty()) {
                g_gpuGroupOpen.front() = true;
            }
            if (!g_cpuGroupOpen.empty()) {
                g_cpuGroupOpen.front() = true;
            }
            RefreshGpus();
            RefreshCpus();
            g_initialized = true;
        }
        DrawUi();
    });
}
