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

constexpr float kPanelPad = 18.0f;

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
std::vector<std::string> g_gpuAdapterLabels;
std::vector<const char*> g_gpuAdapterPtrs;

std::vector<CpuDevice> g_cpus;
int g_selectedCpu = 0;
std::string g_cpuCustomName;
int g_cpuPresetSelected = -1;
char g_cpuSearch[128]{};
std::vector<std::string> g_cpuPresetNames;
std::vector<const char*> g_cpuPresetPtrs;

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

void UpdateGpuAdapterLabels() {
    g_gpuAdapterLabels.clear();
    g_gpuAdapterPtrs.clear();
    for (size_t i = 0; i < g_gpus.size(); ++i) {
        g_gpuAdapterLabels.push_back("GPU " + std::to_string(i + 1) + " — " + WideToUtf8(g_gpus[i].currentName));
        g_gpuAdapterPtrs.push_back(g_gpuAdapterLabels.back().c_str());
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
    UpdateGpuAdapterLabels();
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
    ur::ui::notice("GPU applied — restart Task Manager.");
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
    ur::ui::notice("GPU restored.");
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
    ur::ui::notice("CPU applied — restart Task Manager.");
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
    ur::ui::notice("CPU restored.");
}

void DrawBackground() {
    const float width = static_cast<float>(ur::app::width());
    const float height = static_cast<float>(ur::app::height());
    ur::effects::draw_atmosphere(width, height);
    ur::effects::draw_particles(width, height, Context->DeltaTime);
}

void DrawPageTabs() {
    const bool gpuActive = g_page == Page::Gpu;
    const bool cpuActive = g_page == Page::Cpu;

    if (ur::ui::button(gpuActive ? "GPU  •" : "GPU", 0.0f)) {
        g_page = Page::Gpu;
    }
    Layout->SameLine();
    if (ur::ui::button(cpuActive ? "CPU  •" : "CPU", 0.0f)) {
        g_page = Page::Cpu;
    }
}

void DrawScrollPresetPicker(
    const auto& groups,
    std::vector<std::string>& flatNames,
    std::vector<const char*>& flatPtrs,
    char* search,
    int searchCapacity,
    int& presetSelected,
    std::string& customName,
    const char* searchHint) {
    ur::ui::field("Search", search, searchCapacity, searchHint);

    Layout->Skip(6.0f);
    ur::ui::section("Presets");

    const bool filtering = search[0] != '\0';
    if (filtering) {
        if (Widgets->FilterList(
                "##filter",
                presetSelected,
                flatPtrs.data(),
                static_cast<int>(flatPtrs.size()),
                search,
                searchCapacity,
                11)) {
            if (presetSelected >= 0 && presetSelected < static_cast<int>(flatNames.size())) {
                customName = flatNames[presetSelected];
            }
        }
        return;
    }

    const float scrollHeight = 240.0f * Style->Scale;
    if (!Layout->BeginChild("##preset-scroll", CVector(Layout->Width(), scrollHeight), true)) {
        return;
    }

    for (const auto& group : groups) {
        Widgets->Section(WideToUtf8(group.vendor).c_str());

        std::vector<std::string> localNames;
        std::vector<const char*> localPtrs;
        localNames.reserve(group.models.size());
        localPtrs.reserve(group.models.size());
        for (const auto& model : group.models) {
            localNames.emplace_back(WideToUtf8(model));
            localPtrs.push_back(localNames.back().c_str());
        }

        int localSelected = -1;
        for (size_t i = 0; i < localNames.size(); ++i) {
            if (localNames[i] == customName) {
                localSelected = static_cast<int>(i);
                break;
            }
        }

        if (Widgets->List(
                "##group-list",
                localSelected,
                localPtrs.data(),
                static_cast<int>(localPtrs.size()),
                static_cast<int>(localPtrs.size()))) {
            if (localSelected >= 0 && localSelected < static_cast<int>(localNames.size())) {
                customName = localNames[localSelected];
                SyncPresetSelection(customName, flatNames, presetSelected);
            }
        }

        Layout->Skip(4.0f);
    }

    Layout->EndChild();
}

void DrawStatusBlock(
    const char* emptyMessage,
    const std::string& currentName,
    const char* subLine,
    const char* detailLine) {
    if (currentName.empty()) {
        Widgets->Colored(Style->Warning, emptyMessage);
        ur::ui::faint("Run as Administrator.");
        return;
    }

    ur::ui::faint("Showing as");
    Widgets->Heading(currentName.c_str());
    if (subLine) {
        ur::ui::faint(subLine);
    }
    if (detailLine) {
        ur::ui::faint(detailLine);
    }
}

void DrawActionRow(bool enabled, auto&& onApply, auto&& onRestore) {
    ur::ui::disabled_scope guard(!enabled);
    if (ur::ui::button("Apply", 110.0f)) {
        onApply();
    }
    Layout->SameLine();
    if (ur::ui::button("Restore", 110.0f)) {
        onRestore();
    }
    Layout->SameLine();
    if (ur::ui::button("Refresh", 100.0f)) {
        if (g_page == Page::Gpu) {
            RefreshGpus();
        } else {
            RefreshCpus();
        }
    }
}

void DrawGpuPage() {
    if (g_gpus.size() > 1) {
        ur::ui::section("Adapter");
        if (Widgets->List(
                "##gpu-adapters",
                g_selectedGpu,
                g_gpuAdapterPtrs.data(),
                static_cast<int>(g_gpuAdapterPtrs.size()),
                3)) {
            g_gpuCustomName = WideToUtf8(g_gpus[g_selectedGpu].currentName);
            SyncPresetSelection(g_gpuCustomName, g_gpuPresetNames, g_gpuPresetSelected);
        }
        Layout->Skip(8.0f);
    }

    std::string originalLine;
    if (!g_gpus.empty() && g_gpus[g_selectedGpu].hasCustomName) {
        originalLine = "Original: " + WideToUtf8(g_gpus[g_selectedGpu].originalName);
    }

    DrawStatusBlock(
        "No GPU detected",
        g_gpus.empty() ? std::string{} : WideToUtf8(g_gpus[g_selectedGpu].currentName),
        originalLine.empty() ? nullptr : originalLine.c_str(),
        nullptr);

    if (g_hasLeftover) {
        Layout->Skip(6.0f);
        Widgets->Colored(Style->Warning, "Old virtual adapter detected");
        Layout->SameLine();
        if (ur::ui::button("Remove", 90.0f)) {
            OnGpuCleanupLeftover();
        }
    }

    Layout->Skip(10.0f);
    DrawScrollPresetPicker(
        GetGpuPresets(),
        g_gpuPresetNames,
        g_gpuPresetPtrs,
        g_gpuSearch,
        static_cast<int>(sizeof(g_gpuSearch)),
        g_gpuPresetSelected,
        g_gpuCustomName,
        "4090, Arc B580, 9090...");

    Layout->Skip(8.0f);
    ur::ui::field("Custom name", g_gpuCustomName, "NVIDIA GeForce RTX 4090");
    Layout->Skip(8.0f);
    DrawActionRow(!g_gpus.empty(), OnGpuApply, OnGpuRestore);
}

void DrawCpuPage() {
    std::string originalLine;
    std::string detailLine;
    if (!g_cpus.empty()) {
        const CpuDevice& cpu = g_cpus[g_selectedCpu];
        if (cpu.hasCustomName) {
            originalLine = "Original: " + WideToUtf8(cpu.originalName);
        }
        detailLine = std::to_string(cpu.logicalCores) + " logical cores";
    }

    DrawStatusBlock(
        "No CPU detected",
        g_cpus.empty() ? std::string{} : WideToUtf8(g_cpus[g_selectedCpu].currentName),
        originalLine.empty() ? nullptr : originalLine.c_str(),
        detailLine.empty() ? nullptr : detailLine.c_str());

    Layout->Skip(10.0f);
    DrawScrollPresetPicker(
        GetCpuPresets(),
        g_cpuPresetNames,
        g_cpuPresetPtrs,
        g_cpuSearch,
        static_cast<int>(sizeof(g_cpuSearch)),
        g_cpuPresetSelected,
        g_cpuCustomName,
        "9950X, Ultra 7, Potato...");

    Layout->Skip(8.0f);
    ur::ui::field("Custom name", g_cpuCustomName, "Intel(R) Core(TM) Ultra 7 265K");
    Layout->Skip(8.0f);
    DrawActionRow(!g_cpus.empty(), OnCpuApply, OnCpuRestore);
}

void DrawUi() {
    DrawBackground();

    const float width = static_cast<float>(ur::app::width());
    const float height = static_cast<float>(ur::app::height());
    const float panelWidth = width - kPanelPad * 2.0f;
    const float panelHeight = height - kPanelPad * 2.0f;

    if (Frames->Begin(
            "##panel",
            &g_open,
            FramePin | FrameClose | FrameFit,
            CVector(kPanelPad, kPanelPad),
            CVector(panelWidth, panelHeight))) {
        Widgets->Heading("CPU-GPU-Larp");
        ur::ui::faint("Spoof names in Task Manager · restart Task Manager after apply");

        Layout->Skip(10.0f);
        DrawPageTabs();

        Layout->Skip(12.0f);
        ur::ui::separator();
        Layout->Skip(10.0f);

        if (g_page == Page::Gpu) {
            DrawGpuPage();
        } else {
            DrawCpuPage();
        }

        Layout->Skip(8.0f);
        ur::ui::faint("github.com/ff0l/CPU-GPU-Larp");
    }

    Frames->End();

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
        MessageBoxW(nullptr, L"CPU-GPU-Larp needs Administrator to change device names.", L"CPU-GPU-Larp", MB_ICONWARNING | MB_OK);
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        ShellExecuteW(nullptr, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    ur::app::Config config;
    config.title = "CPU-GPU-Larp";
    config.width = 520;
    config.height = 700;
    config.backend = ur::Backend::Auto;
    config.vsync = true;
    config.docking = false;
    config.persist = false;
    config.media = false;
    config.hear = false;
    config.discord = false;
    config.overlay = true;

    return ur::app::run(config, [] {
        if (!g_initialized) {
            ur::theme::apply(7);
            Style->Glass = true;
            Style->Shadows = true;
            Style->Borders = true;
            ur::effects::set_quality(ur::effects::Quality::Low);
            ur::effects::set_background(1);
            BuildFlatPresets(GetGpuPresets(), g_gpuPresetNames, g_gpuPresetPtrs);
            BuildFlatPresets(GetCpuPresets(), g_cpuPresetNames, g_cpuPresetPtrs);
            RefreshGpus();
            RefreshCpus();
            g_initialized = true;
        }
        DrawUi();
    });
}
