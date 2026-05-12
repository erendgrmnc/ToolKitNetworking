@echo off
setlocal
set "ORIGINAL_APPDATA=%APPDATA%"
if "%~1"=="" (
  set "CONFIG=Debug"
) else (
  set "CONFIG=%~1"
)
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
if defined TK_REPO_ROOT set "ROOT=%TK_REPO_ROOT%"
set "APPDATA=%ROOT%\.codex_tmp\AppData"
set "GDTK_ROOT=%TOOLKIT_ENGINE_ROOT%"
if not defined GDTK_ROOT if exist "%APPDATA%\ToolKit\Config\Path.txt" set /p GDTK_ROOT=<"%APPDATA%\ToolKit\Config\Path.txt"
if not defined GDTK_ROOT if exist "%ORIGINAL_APPDATA%\ToolKit\Config\Path.txt" set /p GDTK_ROOT=<"%ORIGINAL_APPDATA%\ToolKit\Config\Path.txt"
set "LOG_DIR=%ROOT%\.codex_tmp"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
if not defined GDTK_ROOT (
  > "%LOG_DIR%\plugin_ctest.log" echo TOOLKIT_ENGINE_ROOT is not set and no ToolKit engine path is configured in "%APPDATA%\ToolKit\Config\Path.txt" or "%ORIGINAL_APPDATA%\ToolKit\Config\Path.txt".
  > "%LOG_DIR%\plugin_ctest.exit" echo 1
  exit /b 1
)
set "PATH=%ROOT%\Plugins\ToolKitNetworking\Codes\Bin;%GDTK_ROOT%\Bin;%GDTK_ROOT%\Dependency\Intermediate\Windows\%CONFIG%;%PATH%"
cd /d "%ROOT%\Plugins\ToolKitNetworking\Intermediate\Tests"
ctest -C %CONFIG% --output-on-failure > "%LOG_DIR%\plugin_ctest.log" 2>&1
set "ERR=%ERRORLEVEL%"
> "%LOG_DIR%\plugin_ctest.exit" echo %ERR%
exit /b %ERR%
