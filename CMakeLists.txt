cmake_minimum_required(VERSION 3.20)
project(cpu-gpu-larp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{UR_FRAMEWORK_DIR} AND EXISTS "$ENV{UR_FRAMEWORK_DIR}/CMakeLists.txt")
    set(UR_FRAMEWORK_DIR "$ENV{UR_FRAMEWORK_DIR}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../custom-framework/CMakeLists.txt")
    set(UR_FRAMEWORK_DIR "${CMAKE_SOURCE_DIR}/../custom-framework")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../ui Framework/CMakeLists.txt")
    set(UR_FRAMEWORK_DIR "${CMAKE_SOURCE_DIR}/../ui Framework")
else()
    message(FATAL_ERROR "Set UR_FRAMEWORK_DIR or place custom-framework next to this folder.")
endif()

set(UR_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory("${UR_FRAMEWORK_DIR}" ur_framework EXCLUDE_FROM_ALL)

add_executable(cpu-gpu-larp WIN32
    src/main.cxx
    src/gpu_manager.cxx
    src/cpu_manager.cxx
    src/app.rc
)

set_target_properties(cpu-gpu-larp PROPERTIES OUTPUT_NAME "CPU-GPU-Larp")

target_link_libraries(cpu-gpu-larp PRIVATE ur setupapi cfgmgr32 dwmapi)
target_compile_definitions(cpu-gpu-larp PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    target_compile_options(cpu-gpu-larp PRIVATE /W4 /utf-8 /EHsc)
    target_link_options(cpu-gpu-larp PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

add_custom_command(TARGET cpu-gpu-larp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${UR_FRAMEWORK_DIR}/assets"
        $<TARGET_FILE_DIR:cpu-gpu-larp>/assets
)
