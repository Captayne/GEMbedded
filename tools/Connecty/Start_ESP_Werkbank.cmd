@echo off
cd /d "%~dp0"
if exist ".venv\Scripts\python.exe" (
  ".venv\Scripts\python.exe" esp_workbench.py
) else (
  py -3 esp_workbench.py
)
if errorlevel 1 pause
