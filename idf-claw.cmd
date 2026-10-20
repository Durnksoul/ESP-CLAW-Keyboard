@echo off
setlocal
rem Prefer the current ESP-IDF terminal; local paths are optional and untracked.
if defined IDF_PATH goto :check_idf
if not exist "%~dp0idf-local.cmd" goto :missing_idf
call "%~dp0idf-local.cmd"
if errorlevel 1 exit /b %errorlevel%

:check_idf
if not defined IDF_PATH goto :missing_idf
if not exist "%IDF_PATH%\export.bat" goto :missing_idf
set "PYTHONUTF8=1"
set "IDF_TARGET=esp32s3"
if defined IDF_PYTHON_ENV_PATH set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%PATH%"
call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b %errorlevel%
if not defined CLAW_BUILD_DIR set "CLAW_BUILD_DIR=%~dp0build\keyboard"
pushd "%~dp0application\edge_agent"
if errorlevel 1 exit /b %errorlevel%
set "SDKCONFIG_DEFAULTS=%CD%\sdkconfig.defaults;%CD%\components\gen_bmgr_codes\board_manager.defaults;%CD%\sdkconfig.defaults.keyboard"
if /i "%~1"=="init" goto :init
if /i "%~1"=="--version" goto :dispatch
if /i "%~1"=="--help" goto :dispatch
if not exist "components\gen_bmgr_codes\board_manager.defaults" call :generate_board
if errorlevel 1 goto :finish

:dispatch
if "%~1"=="" (
    call :run_idf build
) else (
    call :run_idf %*
)
goto :finish

:init
call :generate_board
goto :finish

:generate_board
call :run_idf bmgr -c ./boards -b esp32_s3_n16r8
exit /b %errorlevel%

:run_idf
python "%IDF_PATH%\tools\idf.py" --no-ccache -B "%CLAW_BUILD_DIR%" -D "SDKCONFIG_DEFAULTS=%SDKCONFIG_DEFAULTS%" %*
exit /b %errorlevel%

:finish
set "CLAW_EXIT_CODE=%errorlevel%"
popd
exit /b %CLAW_EXIT_CODE%

:missing_idf
echo Open an ESP-IDF terminal for ESP-IDF 5.5.4, then run this script again.
echo Alternatively configure the optional, Git-ignored idf-local.cmd.
echo See application/edge_agent/README.md.
exit /b 1
