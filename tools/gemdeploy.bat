@echo off
rem gemdeploy.bat - find a working Python, then upload
rem
rem The Arduino IDE starts this instead of gemdeploy.py, because "python"
rem on Windows is not reliably Python: the PATH usually holds the Store
rem placeholder WindowsApps\python.exe, which only opens the Store.  So
rem every candidate is tried with a tiny program first, and the first one
rem that answers does the work.
rem
rem To use one particular interpreter, set GEMDEPLOY_PYTHON, or write
rem   tools.gemdeploy.cmd.windows=C:/path/to/python.exe
rem into platform.local.txt next to platform.txt.

setlocal enabledelayedexpansion
set "SCRIPT=%~dp0gemdeploy.py"

if defined GEMDEPLOY_PYTHON (
    "%GEMDEPLOY_PYTHON%" "%SCRIPT%" %*
    exit /b %errorlevel%
)

for %%P in (py.exe python3.exe python.exe) do (
    where %%P >nul 2>&1
    if !errorlevel! equ 0 (
        %%P -c "import sys" >nul 2>&1
        if !errorlevel! equ 0 (
            %%P "%SCRIPT%" %*
            exit /b !errorlevel!
        )
    )
)

echo gemdeploy: no working Python found.  Install Python, or set
echo            GEMDEPLOY_PYTHON to the one you want to use.
exit /b 1
