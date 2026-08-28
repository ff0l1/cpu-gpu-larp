cmake_minimum_required(VERSION 3.20)
project(larp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{UR_FRAMEWORK_DIR} AND EXISTS "$ENV{UR_FRAMEWORK_DIR}/CMakeLists.txt")
    set(UR_FRAMEWORK_DIR "$ENV{UR_FRAMEWORK_DIR}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/../ui Framework/CMakeLists.txt")
    set(UR_FRAMEWORK_DIR "${CMAKE_SOURCE_DIR}/../ui Framework")
else()
    message(FATAL_ERROR "Set UR_FRAMEWORK_DIR or place \"ui Framework\" next to this folder.")
endif()

set(UR_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory("${UR_FRAMEWORK_DIR}" ur_framework EXCLUDE_FROM_ALL)

add_executable(larp WIN32
    src/main.cpp
    src/gpu_manager.cpp
    src/cpu_manager.cpp
    src/app.rc
)

target_link_libraries(larp PRIVATE ur setupapi cfgmgr32 dwmapi)
target_compile_definitions(larp PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    target_compile_options(larp PRIVATE /W4 /utf-8 /EHsc)
    target_link_options(larp PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

add_custom_command(TARGET larp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${UR_FRAMEWORK_DIR}/assets"
        $<TARGET_FILE_DIR:larp>/assets
)
