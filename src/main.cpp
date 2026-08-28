#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <shellapi.h>
#include <dwmapi.h>

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

#pragma comment(lib, "dwmapi.lib")

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
bool g_borderlessApplied = false;

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
        const std::string prefix = WideToUtf8(group.vendor) + " — ";
        for (const auto& model : group.models) {
            names.emplace_back(prefix + WideToUtf8(model));
            ptrs.push_back(names.back().c_str());
        }
    }
}

void SyncPresetSelection(const std::string& customName, const std::vector<std::string>& names, int& selected) {
    selected = -1;
    for (size_t i = 0; i < names.size(); ++i) {
        const auto dash = names[i].find(" — ");
        const std::string model = dash != std::string::npos ? names[i].substr(dash + 3) : names[i];
        if (_stricmp(model.c_str(), customName.c_str()) == 0) {
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

void ApplyBorderlessOverlay() {
    HWND hwnd = static_cast<HWND>(ur::app::window());
    if (!hwnd) {
        return;
    }

    const int width = ur::app::width();
    const int height = ur::app::height();
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER);
    style |= WS_POPUP;
    SetWindowLongW(hwnd, GWL_STYLE, style);

    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOPMOST;
    SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        (screenW - width) / 2,
        (screenH - height) / 2,
        width,
        height,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

void HandlePanelDrag(const CRectangle& panel) {
    HWND hwnd = static_cast<HWND>(ur::app::window());
    if (!hwnd) {
        return;
    }

    const CRectangle drag(panel.Left, panel.Top, panel.Width, 36.0f * Style->Scale);
    if (Input->MousePressed(0) && drag.Contains(Input->MousePosition) && Context->ActiveItem == 0) {
        ReleaseCapture();
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
}
    const float width = static_cast<float>(ur::app::width());
    const float height = static_cast<float>(ur::app::height());
    ur::effects::draw_atmosphere(width, height);
    ur::effects::draw_particles(width, height, Context->DeltaTime);
}

void DrawGlassPanel(const CRectangle& panel) {
    if (Style->Shadows) {
        Canvas->Shadow(panel, Style->Shade, Style->Rounding, Style->Softness * Style->Elevation);
    }
    if (Style->Glass) {
        Canvas->Gradient(
            panel,
            Style->Surface.Blend(CColor(255, 255, 255), 0.06f),
            Style->Surface.Blend(CColor(0, 0, 0), 0.15f),
            Style->Rounding,
            false);
    } else {
        Canvas->Rectangle(panel, Style->Surface, Style->Rounding);
    }
    if (Style->Borders) {
        Canvas->Border(panel, Style->Outline.Fade(0.65f), Style->Rounding, Style->Thickness);
    }
}

bool DrawPresetList(
    const std::vector<std::string>& names,
    const std::vector<const char*>& ptrs,
    char* search,
    int searchCapacity,
    int& selected,
    std::string& customName,
    const char* hint) {
    ur::ui::field("Search presets", search, searchCapacity, hint);

    std::vector<int> hits;
    hits.reserve(ptrs.size());
    for (int i = 0; i < static_cast<int>(ptrs.size()); ++i) {
        if (search[0] == '\0' || (ptrs[i] && strstr(ptrs[i], search))) {
            hits.push_back(i);
        }
    }

    Layout->Skip(6.0f);
    ur::ui::section("Presets");

    bool changed = false;
    int first = 0;
    int last = 0;
    const float rowHeight = Style->ControlHeight * 0.88f;
    const float listHeight = rowHeight * 11.0f;

    if (Widgets->BeginVirtual("##presets", static_cast<int>(hits.size()), rowHeight, first, last, listHeight)) {
        for (int slot = first; slot < last; ++slot) {
            const int index = hits[static_cast<size_t>(slot)];
            Context->PushIdentifier(index);
            if (Widgets->Selectable(ptrs[index], selected == index, rowHeight)) {
                selected = index;
                const auto dash = names[index].find(" — ");
                customName = dash != std::string::npos ? names[index].substr(dash + 3) : names[index];
                changed = true;
            }
            Context->PopIdentifier();
        }
        Widgets->EndVirtual();
    }

    return changed;
}

void DrawStatus(const char* emptyMessage, const std::string& currentName, const char* subLine, const char* detailLine) {
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

void DrawActions(bool enabled, auto&& onApply, auto&& onRestore) {
    ur::ui::disabled_scope guard(!enabled);
    if (ur::ui::button("Apply", 120.0f)) {
        onApply();
    }
    Layout->SameLine();
    if (ur::ui::button("Restore", 120.0f)) {
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

void OnGpuApply() {
    if (g_gpus.empty()) {
        return;
    }
    const std::wstring name = Utf8ToWide(g_gpuCustomName);
    if (name.empty()) {
        ur::ui::notice("Pick a GPU name.");
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
    if (g_gpus.empty() || !GpuManager::RemoveFriendlyName(g_gpus[g_selectedGpu])) {
        ur::ui::notice("Could not restore the GPU name.");
        return;
    }
    RefreshGpus();
    ur::ui::notice("GPU restored.");
}

void OnCpuApply() {
    if (g_cpus.empty()) {
        return;
    }
    const std::wstring name = Utf8ToWide(g_cpuCustomName);
    if (name.empty()) {
        ur::ui::notice("Pick a CPU name.");
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
    if (g_cpus.empty() || !CpuManager::RemoveFriendlyName(g_cpus[g_selectedCpu])) {
        ur::ui::notice("Could not restore the CPU name.");
        return;
    }
    RefreshCpus();
    ur::ui::notice("CPU restored.");
}

void DrawGpuPage() {
    if (g_gpus.size() > 1) {
        ur::ui::section("Adapter");
        for (int i = 0; i < static_cast<int>(g_gpuAdapterPtrs.size()); ++i) {
            Context->PushIdentifier(i);
            if (Widgets->Selectable(g_gpuAdapterPtrs[i], g_selectedGpu == i)) {
                g_selectedGpu = i;
                g_gpuCustomName = WideToUtf8(g_gpus[g_selectedGpu].currentName);
                SyncPresetSelection(g_gpuCustomName, g_gpuPresetNames, g_gpuPresetSelected);
            }
            Context->PopIdentifier();
        }
        Layout->Skip(8.0f);
    }

    std::string originalLine;
    if (!g_gpus.empty() && g_gpus[g_selectedGpu].hasCustomName) {
        originalLine = "Original: " + WideToUtf8(g_gpus[g_selectedGpu].originalName);
    }

    DrawStatus(
        "No GPU detected",
        g_gpus.empty() ? std::string{} : WideToUtf8(g_gpus[g_selectedGpu].currentName),
        originalLine.empty() ? nullptr : originalLine.c_str(),
        nullptr);

    if (g_hasLeftover) {
        Layout->Skip(6.0f);
        Widgets->Colored(Style->Warning, "Old virtual adapter detected");
        Layout->SameLine();
        if (ur::ui::button("Remove", 90.0f)) {
            if (GpuManager::RemoveLeftoverVirtualAdapter()) {
                RefreshGpus();
            }
        }
    }

    Layout->Skip(10.0f);
    DrawPresetList(
        g_gpuPresetNames,
        g_gpuPresetPtrs,
        g_gpuSearch,
        static_cast<int>(sizeof(g_gpuSearch)),
        g_gpuPresetSelected,
        g_gpuCustomName,
        "4090, Arc, Fun...");

    Layout->Skip(8.0f);
    ur::ui::field("Custom name", g_gpuCustomName, "NVIDIA GeForce RTX 4090");
    Layout->Skip(10.0f);
    DrawActions(!g_gpus.empty(), OnGpuApply, OnGpuRestore);
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

    DrawStatus(
        "No CPU detected",
        g_cpus.empty() ? std::string{} : WideToUtf8(g_cpus[g_selectedCpu].currentName),
        originalLine.empty() ? nullptr : originalLine.c_str(),
        detailLine.empty() ? nullptr : detailLine.c_str());

    Layout->Skip(10.0f);
    DrawPresetList(
        g_cpuPresetNames,
        g_cpuPresetPtrs,
        g_cpuSearch,
        static_cast<int>(sizeof(g_cpuSearch)),
        g_cpuPresetSelected,
        g_cpuCustomName,
        "9950X, Ultra, Potato...");

    Layout->Skip(8.0f);
    ur::ui::field("Custom name", g_cpuCustomName, "Intel(R) Core(TM) Ultra 7 265K");
    Layout->Skip(10.0f);
    DrawActions(!g_cpus.empty(), OnCpuApply, OnCpuRestore);
}

void DrawUi() {
    if (!g_borderlessApplied) {
        ApplyBorderlessOverlay();
        g_borderlessApplied = true;
    }

    DrawBackground();

    const float width = static_cast<float>(ur::app::width());
    const float height = static_cast<float>(ur::app::height());
    const float margin = 10.0f * Style->Scale;
    const CRectangle panel(margin, margin, width - margin * 2.0f, height - margin * 2.0f);
    DrawGlassPanel(panel);
    HandlePanelDrag(panel);

    const float pad = Style->PaddingWide;
    Layout->Begin(CRectangle(panel.Left + pad, panel.Top + pad, panel.Width - pad * 2.0f, panel.Height - pad * 2.0f));

    Widgets->Heading("CPU-GPU-Larp");
    Layout->SameLine(Layout->Width() - 36.0f * Style->Scale);
    if (ur::ui::button("X", 32.0f)) {
        ur::app::quit();
    }

    ur::ui::faint("Spoof GPU or CPU in Task Manager");
    Layout->Skip(10.0f);

    if (Widgets->BeginTabs("##mode")) {
        if (Widgets->Tab("GPU")) {
            g_page = Page::Gpu;
        }
        if (Widgets->Tab("CPU")) {
            g_page = Page::Cpu;
        }
        Widgets->EndTabs();
    }

    Layout->Skip(12.0f);
    ur::ui::separator();
    Layout->Skip(10.0f);

    if (g_page == Page::Gpu) {
        DrawGpuPage();
    } else {
        DrawCpuPage();
    }

    Layout->End();
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
    config.width = 500;
    config.height = 660;
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
            Style->Backdrop = CColor(8, 8, 14, 255);
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
