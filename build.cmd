@echo off
setlocal
rem Builds the VST3 + Standalone with MSVC (Visual Studio) via CMake + Ninja.
rem Build output goes to %LOCALAPPDATA%\MusicLearnTrainer\build so the vault stays clean.

set "SRC=%~dp0"
set "BUILD=%LOCALAPPDATA%\MusicLearnTrainer\build"

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS ( echo Visual Studio with C++ tools not found & exit /b 1 )

for /f "usebackq tokens=*" %%i in (`python -c "import cmake;print(cmake.CMAKE_BIN_DIR)"`) do set "CMAKE_BIN=%%i"
for /f "usebackq tokens=*" %%i in (`python -c "import ninja;print(ninja.BIN_DIR)"`) do set "NINJA_BIN=%%i"
set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%PATH%"

call "%VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1

cmake -S "%SRC%." -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%BUILD%" --target MusicLearnTrainer_VST3 MusicLearnTrainer_Standalone || exit /b 1

echo.
echo Build finished. Artefacts:
dir /s /b "%BUILD%\MusicLearnTrainer_artefacts\*.vst3" "%BUILD%\MusicLearnTrainer_artefacts\*.exe" 2>nul
