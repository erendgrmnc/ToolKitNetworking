@echo off
setlocal
if "%~1"=="" (
  set "CONFIG=Debug"
) else (
  set "CONFIG=%~1"
)
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
if defined TK_REPO_ROOT set "ROOT=%TK_REPO_ROOT%"
set "LOG_DIR=%ROOT%\.codex_tmp"
if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
set "APPDATA=%ROOT%\.codex_tmp\AppData"
cd /d "%ROOT%\Plugins\ToolKitNetworking\Codes"
cmake --build ..\Intermediate\Plugin --config %CONFIG% --target ToolKitNetworking > "%LOG_DIR%\plugin_build.log" 2>&1
set "ERR=%ERRORLEVEL%"
> "%LOG_DIR%\plugin_build.exit" echo %ERR%
exit /b %ERR%
