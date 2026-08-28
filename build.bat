@echo off
setlocal
cd /d "%~dp0"

if not defined UR_FRAMEWORK_DIR (
  if exist "%~dp0..\ui Framework\CMakeLists.txt" set "UR_FRAMEWORK_DIR=%~dp0..\ui Framework"
)

set "CMAKE=cmake"
where cmake >nul 2>&1 || set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

"%CMAKE%" -S . -B build -A x64 || exit /b 1
"%CMAKE%" --build build --config Release || exit /b 1

echo.
echo Built: build\Release\larp.exe
echo Run as Administrator.
