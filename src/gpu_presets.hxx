#pragma once

#include <string_view>
#include <vector>

struct GpuPresetGroup {
    std::wstring_view vendor;
    std::vector<std::wstring_view> models;
};

inline const std::vector<GpuPresetGroup>& GetGpuPresets() {
    static const std::vector<GpuPresetGroup> presets = {
        { L"NVIDIA RTX 50", { L"NVIDIA GeForce RTX 5090", L"NVIDIA GeForce RTX 5080", L"NVIDIA GeForce RTX 5070 Ti", L"NVIDIA GeForce RTX 5070", L"NVIDIA GeForce RTX 5060 Ti", L"NVIDIA GeForce RTX 5060" } },
        { L"NVIDIA RTX 40", { L"NVIDIA GeForce RTX 4090", L"NVIDIA GeForce RTX 4080 SUPER", L"NVIDIA GeForce RTX 4080", L"NVIDIA GeForce RTX 4070 Ti SUPER", L"NVIDIA GeForce RTX 4070 Ti", L"NVIDIA GeForce RTX 4070 SUPER", L"NVIDIA GeForce RTX 4070", L"NVIDIA GeForce RTX 4060 Ti", L"NVIDIA GeForce RTX 4060" } },
        { L"NVIDIA RTX 30", { L"NVIDIA GeForce RTX 3090 Ti", L"NVIDIA GeForce RTX 3090", L"NVIDIA GeForce RTX 3080 Ti", L"NVIDIA GeForce RTX 3080", L"NVIDIA GeForce RTX 3070 Ti", L"NVIDIA GeForce RTX 3070", L"NVIDIA GeForce RTX 3060 Ti", L"NVIDIA GeForce RTX 3060", L"NVIDIA GeForce RTX 3050" } },
        { L"NVIDIA RTX 20", { L"NVIDIA GeForce RTX 2080 Ti", L"NVIDIA GeForce RTX 2080 SUPER", L"NVIDIA GeForce RTX 2070 SUPER", L"NVIDIA GeForce RTX 2060 SUPER", L"NVIDIA GeForce RTX 2060" } },
        { L"NVIDIA GTX", { L"NVIDIA GeForce GTX 1660 Ti", L"NVIDIA GeForce GTX 1660 SUPER", L"NVIDIA GeForce GTX 1080 Ti", L"NVIDIA GeForce GTX 1070", L"NVIDIA GeForce GTX 1060 6GB" } },
        { L"AMD RX 9000", { L"AMD Radeon RX 9070 XT", L"AMD Radeon RX 9070", L"AMD Radeon RX 9060 XT" } },
        { L"AMD RX 7000", { L"AMD Radeon RX 7900 XTX", L"AMD Radeon RX 7900 XT", L"AMD Radeon RX 7800 XT", L"AMD Radeon RX 7700 XT", L"AMD Radeon RX 7600" } },
        { L"AMD RX 6000", { L"AMD Radeon RX 6950 XT", L"AMD Radeon RX 6900 XT", L"AMD Radeon RX 6800 XT", L"AMD Radeon RX 6700 XT", L"AMD Radeon RX 6600 XT" } },
        { L"Intel Arc B-Series", { L"Intel(R) Arc(TM) B580 Graphics", L"Intel(R) Arc(TM) B570 Graphics", L"Intel(R) Arc(TM) B550 Graphics" } },
        { L"Intel Arc A-Series", { L"Intel(R) Arc(TM) A770 Graphics", L"Intel(R) Arc(TM) A750 Graphics", L"Intel(R) Arc(TM) A580 Graphics", L"Intel(R) Arc(TM) A380 Graphics" } },
        { L"Fun", {
            L"NVIDIA GeForce RTX 9090 Ti SUPER",
            L"AMD Radeon RX 9990 XTX 32GB",
            L"Intel(R) Arc(TM) B9990 Graphics",
            L"NVIDIA GeForce GT 710",
            L"Microsoft Remote Display Adapter",
        } },
    };
    return presets;
}
