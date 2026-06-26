@echo off
REM OTF Browser crash report launcher.
REM
REM Double-click this file, or run it from cmd, to capture a full diagnostic
REM report for a crashing otf-browser.exe. It launches the PowerShell
REM collector with execution policy bypassed so you do not need to change
REM system settings first.
REM
REM Usage: run-crash-report.cmd [seconds] [otf-browser.exe args...]
REM Example: run-crash-report.cmd 30 --no-sandbox

setlocal
set SECONDS=%1
if "%SECONDS%"=="" set SECONDS=30

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect-windows-crash-report.ps1" -Seconds %SECONDS% %2 %3 %4 %5 %6 %7 %8 %9
endlocal