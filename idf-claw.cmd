@echo off
setlocal
set "IDF_TOOLS_PATH=C:\Espressif"
set "IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v5.5.4\venv"
set "PYTHONUTF8=1"
set "IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.cn"
set "IDF_COMPONENT_API_TIMEOUT=60"
set "NO_PROXY=localhost,127.0.0.1,components-file.espressif.cn"
set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%PATH%"
cd /d "%~dp0application\edge_agent"
call "D:\esp\v5.5.4\esp-idf\export.bat"
if errorlevel 1 exit /b %errorlevel%
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "D:\esp\v5.5.4\esp-idf\tools\idf.py" --no-ccache -B "%~dp0build\keyboard" %*
exit /b %errorlevel%


