#pragma once

#include <string_view>
#include <vector>

struct CpuPresetGroup {
    std::wstring_view vendor;
    std::vector<std::wstring_view> models;
};

inline const std::vector<CpuPresetGroup>& GetCpuPresets() {
    static const std::vector<CpuPresetGroup> presets = {
        {
            L"Intel Core Ultra",
            {
                L"Intel(R) Core(TM) Ultra 9 285K",
                L"Intel(R) Core(TM) Ultra 7 265K",
                L"Intel(R) Core(TM) Ultra 5 245K",
            },
        },
        {
            L"Intel Core 14th Gen",
            {
                L"Intel(R) Core(TM) i9-14900KS",
                L"Intel(R) Core(TM) i9-14900K",
                L"Intel(R) Core(TM) i7-14700K",
                L"Intel(R) Core(TM) i5-14600K",
            },
        },
        {
            L"Intel Core 13th Gen",
            {
                L"Intel(R) Core(TM) i9-13900KS",
                L"Intel(R) Core(TM) i9-13900K",
                L"Intel(R) Core(TM) i7-13700K",
                L"Intel(R) Core(TM) i5-13600K",
            },
        },
        {
            L"Intel Core 12th Gen",
            {
                L"Intel(R) Core(TM) i9-12900KS",
                L"Intel(R) Core(TM) i9-12900K",
                L"Intel(R) Core(TM) i7-12700K",
                L"Intel(R) Core(TM) i5-12600K",
            },
        },
        {
            L"AMD Ryzen 9000",
            {
                L"AMD Ryzen 9 9950X3D 16-Core Processor",
                L"AMD Ryzen 9 9950X 16-Core Processor",
                L"AMD Ryzen 9 9900X 12-Core Processor",
                L"AMD Ryzen 7 9800X3D 8-Core Processor",
                L"AMD Ryzen 7 9700X 8-Core Processor",
                L"AMD Ryzen 5 9600X 6-Core Processor",
            },
        },
        {
            L"AMD Ryzen 7000",
            {
                L"AMD Ryzen 9 7950X3D 16-Core Processor",
                L"AMD Ryzen 9 7950X 16-Core Processor",
                L"AMD Ryzen 9 7900X 12-Core Processor",
                L"AMD Ryzen 7 7800X3D 8-Core Processor",
                L"AMD Ryzen 7 7700X 8-Core Processor",
                L"AMD Ryzen 5 7600X 6-Core Processor",
            },
        },
        {
            L"AMD Ryzen 5000",
            {
                L"AMD Ryzen 9 5950X 16-Core Processor",
                L"AMD Ryzen 9 5900X 12-Core Processor",
                L"AMD Ryzen 7 5800X3D 8-Core Processor",
                L"AMD Ryzen 7 5800X 8-Core Processor",
                L"AMD Ryzen 5 5600X 6-Core Processor",
            },
        },
        {
            L"AMD Ryzen 3000",
            {
                L"AMD Ryzen 9 3950X 16-Core Processor",
                L"AMD Ryzen 9 3900X 12-Core Processor",
                L"AMD Ryzen 7 3700X 8-Core Processor",
                L"AMD Ryzen 5 3600 6-Core Processor",
            },
        },
        {
            L"Fun",
            {
                L"Intel(R) Core(TM) i9-99900KS",
                L"AMD Ryzen 9 9950X3D 32-Core Processor",
                L"Potato CPU 1-Core Processor",
                L"Intel(R) Celeron(R) D 360",
                L"AMD Athlon(tm) XP 3200+",
            },
        },
    };
    return presets;
}
