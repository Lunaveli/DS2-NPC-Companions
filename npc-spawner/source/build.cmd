@echo off
rem Builds NPCSpawner.dll and dinput8.dll for one variant: build.cmd main  or  build.cmd seeker
rem Needs Visual Studio 2022 or newer (Desktop C++ workload) and MinHook built for x64 (set MINHOOK below).
setlocal
if "%~1"=="" (echo usage: build.cmd main^|seeker & exit /b 1)
if not defined MINHOOK set MINHOOK=%~dp0third_party\minhook
if not defined VSDEVCMD for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSDEVCMD=%%i\VC\Auxiliary\Build\vcvars64.bat
call "%VSDEVCMD%" >nul || exit /b 1
set OUT=%~dp0out\%~1
mkdir "%OUT%" 2>nul
cd /d "%~dp0%~1" || exit /b 1
set IMGUI=..\third_party\imgui
cl /nologo /LD /MT /O2 /W3 /std:c++17 /EHsc /I"%MINHOOK%\include" /I..\third_party loader.cpp %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp %IMGUI%\imgui_impl_dx11.cpp %IMGUI%\imgui_impl_win32.cpp /Fo"%OUT%\\" /link /OUT:"%OUT%\NPCSpawner.dll" "%MINHOOK%\lib\libMinHook.x64.lib" bcrypt.lib user32.lib d3d11.lib dxgi.lib dinput8.lib dxguid.lib d3dcompiler.lib || exit /b 1
ml64 /nologo /c /Fo"%OUT%\proxy.obj" proxy.asm || exit /b 1
cl /nologo /LD /MT /O2 /W4 /std:c++17 /EHsc bootstrap.cpp "%OUT%\proxy.obj" /Fo"%OUT%\\" /link /DEF:proxy.def /OUT:"%OUT%\dinput8.dll" user32.lib || exit /b 1
echo Built %OUT%\NPCSpawner.dll and %OUT%\dinput8.dll
