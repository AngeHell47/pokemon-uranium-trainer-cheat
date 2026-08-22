@echo off
setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
    echo Visual Studio 2022 avec les outils C++ x86 est requis.
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 exit /b 1

if not exist "build" mkdir "build"
if not exist "build\payload_obj" mkdir "build\payload_obj"

set "DLLSRC=..\Launcher DLL"

rc /nologo /fo build\payload_resources.res payload_resources.rc
if errorlevel 1 exit /b 1

cl /nologo /O1 /GS- /MT /LD /W4 /utf-8 /DTRAINER_EXTERNAL_PAYLOAD ^
  "%DLLSRC%\version_proxy.cpp" ^
  "%DLLSRC%\rgss_safe_dispatch.cpp" ^
  "%DLLSRC%\trainer_menu.cpp" ^
  "%DLLSRC%\trainer_editors.cpp" ^
  "%DLLSRC%\moves_db.cpp" ^
  "%DLLSRC%\options\opt_startup.cpp" ^
  "%DLLSRC%\options\opt_pause.cpp" ^
  "%DLLSRC%\options\opt_hp.cpp" ^
  "%DLLSRC%\options\opt_godmode_repair.cpp" ^
  "%DLLSRC%\options\opt_pp.cpp" ^
  "%DLLSRC%\options\opt_capture.cpp" ^
  "%DLLSRC%\options\opt_egghatch.cpp" ^
  "%DLLSRC%\options\opt_hmforget.cpp" ^
  "%DLLSRC%\options\opt_money.cpp" ^
  "%DLLSRC%\options\opt_inventory_manager.cpp" ^
  "%DLLSRC%\options\opt_noclip.cpp" ^
  "%DLLSRC%\options\opt_gamespeed.cpp" ^
  "%DLLSRC%\options\opt_speed.cpp" ^
  "%DLLSRC%\options\opt_zoom.cpp" ^
  "%DLLSRC%\options\opt_noenc.cpp" ^
  "%DLLSRC%\options\opt_encounter.cpp" ^
  "%DLLSRC%\options\opt_pokemon_manager.cpp" ^
  "%DLLSRC%\options\opt_time.cpp" ^
  "%DLLSRC%\options\opt_weather.cpp" ^
  "%DLLSRC%\options\opt_heal.cpp" ^
  "%DLLSRC%\options\opt_extras.cpp" ^
  build\payload_resources.res ^
  /Fo:build\payload_obj\ /Fe:build\trainer_payload.dll ^
  /link /MACHINE:X86 /MAP:build\trainer_payload.map ^
  psapi.lib kernel32.lib user32.lib gdi32.lib d3d9.lib
if errorlevel 1 exit /b 1

rc /nologo /fo trainer_external.res trainer_external.rc
if errorlevel 1 exit /b 1

cl /nologo /O2 /GS /MT /EHsc /std:c++17 /utf-8 trainer_external.cpp trainer_external.res ^
  /Fe:UraniumTrainer.exe /link /SUBSYSTEM:WINDOWS /MACHINE:X86 user32.lib gdi32.lib comctl32.lib
if errorlevel 1 exit /b 1

echo.
echo Build termine : %CD%\UraniumTrainer.exe
endlocal
