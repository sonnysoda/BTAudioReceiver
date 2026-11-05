@echo off
rem Build the WinUI 3 shell (C++/WinRT).
rem Prerequisites:
rem   - Visual Studio 2022 with the "Desktop development with C++" workload
rem   - Windows App SDK C++ components (VS Installer - Individual components)
rem   - Rust toolchain (x86_64-pc-windows-msvc)
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo VSWHERE_NOT_FOUND: install Visual Studio 2022 with the C++ workload
  exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo VS_NOT_FOUND: no Visual Studio installation with C++ tools
  exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo VCVARS_FAILED
  exit /b 1
)

set "MSBUILD_EXE=%VSDIR%\MSBuild\Current\Bin\MSBuild.exe"

cd /d "%~dp0"

if not exist "..\core\target\release\btcore.dll.lib" (
  echo CORE_NOT_BUILT: run "cargo build --release" inside ..\core first
  exit /b 1
)

"%MSBUILD_EXE%" BTAudioReceiver.vcxproj /restore /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
if errorlevel 1 (
  echo BUILD_FAILED
  exit /b 1
)

echo BUILD_OK
echo Output: x64\Release\BTAudioReceiver\BTAudioReceiver.exe
