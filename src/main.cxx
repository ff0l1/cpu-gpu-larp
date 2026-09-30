#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <shellapi.h>
#include <dwmapi.h>

#include "ur/ur.hxx"
#include "gpu_manager.hxx"
#include "cpu_manager.hxx"
#include "gpu_presets.hxx"
#include "cpu_presets.hxx"

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

std::string SanitizeUtf8Name(std::string value) {
    while (value.starts_with("\xEF\xBF\xBD")) {
        value.erase(0, 3);
    }
    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    return value;
}

Page g_page = Page::Gpu;
int g_pageIndex = 0;
bool g_initialized = false;
bool g_borderlessApplied = false;
bool g_dragging = false;
POINT g_dragStart{};
RECT g_dragWindow{};

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

constexpr std::string_view kPresetSeparator = " — ";

std::string ExtractPresetModel(std::string_view label) {
    const auto pos = label.find(kPresetSeparator);
    if (pos == std::string_view::npos) {
        return std::string(label);
    }
    return std::string(label.substr(pos + kPresetSeparator.size()));
}

void BuildFlatPresets(
    const auto& groups,
    std::vector<std::string>& names,
    std::vector<const char*>& ptrs) {
    names.clear();
    ptrs.clear();
    for (const auto& group : groups) {
        const std::string prefix = WideToUtf8(group.vendor) + std::string(kPresetSeparator);
        for (const auto& model : group.models) {
            names.emplace_back(prefix + WideToUtf8(model));
            ptrs.push_back(names.back().c_str());
        }
    }
}

void SyncPresetSelection(const std::string& customName, const std::vector<std::string>& names, int& selected) {
    selected = -1;
    for (size_t i = 0; i < names.size(); ++i) {
        if (_stricmp(ExtractPresetModel(names[i]).c_str(), customName.c_str()) == 0) {
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
    g_gpuCustomName = SanitizeUtf8Name(WideToUtf8(g_gpus[g_selectedGpu].currentName));
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
    g_cpuCustomName = SanitizeUtf8Name(WideToUtf8(g_cpus[g_selectedCpu].currentName));
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

void DrawBackground() {
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

void HandlePanelDrag(const CRectangle& panel) {
    HWND hwnd = static_cast<HWND>(ur::app::window());
    if (!hwnd) {
        return;
    }

    const CRectangle drag(
        panel.Left,
        panel.Top,
        panel.Width - 52.0f * Style->Scale,
        44.0f * Style->Scale);
    const bool inDragZone = drag.Contains(Input->MousePosition) && Context->ActiveItem == 0;

    if (Input->MousePressed(0) && inDragZone) {
        g_dragging = true;
        GetCursorPos(&g_dragStart);
        GetWindowRect(hwnd, &g_dragWindow);
    }

    if (g_dragging) {
        if (Input->MouseDown(0)) {
            POINT now{};
            GetCursorPos(&now);
            SetWindowPos(
                hwnd,
                nullptr,
                g_dragWindow.left + (now.x - g_dragStart.x),
                g_dragWindow.top + (now.y - g_dragStart.y),
                0,
                0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            g_dragging = false;
        }
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

    Layout->Skip(8.0f);
    ur::ui::section("Presets");

    const float footerReserve = Style->ControlHeight * 2.0f + Style->Spacing * 5.0f + 36.0f * Style->Scale;
    const float rowHeight = Style->ControlHeight * 0.92f;
    float listHeight = Layout->Remaining() - footerReserve;
    const float minList = rowHeight * 5.0f;
    if (listHeight < minList) {
        listHeight = minList;
    }
    const float maxList = Layout->Remaining() - Style->Spacing * 2.0f;
    if (listHeight > maxList) {
        listHeight = maxList;
    }

    bool changed = false;
    if (Layout->BeginChild("##presets", CVector(Layout->Width(), listHeight), false)) {
        for (int slot = 0; slot < static_cast<int>(hits.size()); ++slot) {
            const int index = hits[static_cast<size_t>(slot)];
            Context->PushIdentifier(index);
            if (Widgets->Selectable(ptrs[index], selected == index, rowHeight)) {
                selected = index;
                customName = ExtractPresetModel(names[index]);
                changed = true;
            }
            Context->PopIdentifier();
        }
        Layout->EndChild();
    }

    return changed;
}

bool DrawCloseButton(float size) {
    const CRectangle bounds = Layout->Place(CVector(size, size));

    bool hovered = false;
    bool held = false;
    const bool clicked = Widgets->Hit("##close", bounds, hovered, held);

    const float round = size * 0.35f;
    CColor fill = Style->Control;
    if (hovered) {
        fill = hovered && held ? Style->Pressed : Style->Hovered;
    }
    if (hovered) {
        Canvas->Rectangle(bounds, Style->Danger.Fade(0.88f), round);
    } else {
        Canvas->Rectangle(bounds, fill, round);
        if (Style->Borders) {
            Canvas->Border(bounds, Style->Outline.Fade(0.7f), round, Style->Thickness);
        }
    }

    const CRectangle mark = bounds.Shrink(size * 0.30f);
    const CColor ink = hovered ? CColor(255, 255, 255) : Style->Faint;
    const float line = Style->IconStroke * 1.15f;
    Canvas->Stroke(mark, CVector(0.08f, 0.08f), CVector(0.92f, 0.92f), ink, line);
    Canvas->Stroke(mark, CVector(0.92f, 0.08f), CVector(0.08f, 0.92f), ink, line);

    return clicked;
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
    if (ur::ui::button("Apply", 132.0f)) {
        onApply();
    }
    Layout->SameLine();
    if (ur::ui::button("Restore", 132.0f)) {
        onRestore();
    }
    Layout->SameLine();
    if (ur::ui::button("Refresh", 112.0f)) {
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
    const std::wstring name = Utf8ToWide(SanitizeUtf8Name(g_gpuCustomName));
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
    const std::wstring name = Utf8ToWide(SanitizeUtf8Name(g_cpuCustomName));
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
                g_gpuCustomName = SanitizeUtf8Name(WideToUtf8(g_gpus[g_selectedGpu].currentName));
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

    Layout->Skip(10.0f);
    ur::ui::field("Custom name", g_gpuCustomName, "NVIDIA GeForce RTX 4090");
    Layout->Skip(12.0f);
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

    Layout->Skip(10.0f);
    ur::ui::field("Custom name", g_cpuCustomName, "Intel(R) Core(TM) Ultra 7 265K");
    Layout->Skip(12.0f);
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
    const float margin = 8.0f * Style->Scale;
    const CRectangle panel(margin, margin, width - margin * 2.0f, height - margin * 2.0f);
    DrawGlassPanel(panel);
    HandlePanelDrag(panel);

    const float pad = Style->PaddingWide * 1.1f;
    Layout->Begin(CRectangle(panel.Left + pad, panel.Top + pad, panel.Width - pad * 2.0f, panel.Height - pad * 2.0f));

    Widgets->Heading("CPU-GPU-Larp");
    Layout->SameLine(Layout->Width() - 40.0f * Style->Scale);
    if (DrawCloseButton(36.0f * Style->Scale)) {
        ur::app::quit();
    }

    ur::ui::faint("Spoof GPU or CPU in Task Manager");
    Layout->Skip(12.0f);

    const char* modes[] = { "GPU", "CPU" };
    Widgets->Segments("##mode", g_pageIndex, modes, 2);
    g_page = g_pageIndex == 0 ? Page::Gpu : Page::Cpu;

    Layout->Skip(14.0f);
    ur::ui::separator();
    Layout->Skip(12.0f);

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

}

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
    config.width = 560;
    config.height = 780;
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
            Style->Scale = 1.12f;
            Style->Glass = true;
            Style->Shadows = true;
            Style->Borders = true;
            Style->Rounding = 14.0f;
            Style->ControlHeight = 38.0f;
            Style->Backdrop = CColor(8, 8, 14, 255);
            ur::effects::set_quality(ur::effects::Quality::Low);
            ur::effects::set_background(1);
            ur::app::overlay_options().topmost = true;
            BuildFlatPresets(GetGpuPresets(), g_gpuPresetNames, g_gpuPresetPtrs);
            BuildFlatPresets(GetCpuPresets(), g_cpuPresetNames, g_cpuPresetPtrs);
            RefreshGpus();
            RefreshCpus();
            ApplyBorderlessOverlay();
            g_borderlessApplied = true;
            g_initialized = true;
        }
        DrawUi();
    });
}
