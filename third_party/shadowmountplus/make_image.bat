@echo off
setlocal EnableExtensions

REM make_image.bat
REM Usage: make_image.bat "C:\images\data.exfat" "C:\payload"

if "%~1"=="" goto :usage
if "%~2"=="" goto :usage

set "IMAGE=%~1"
set "SRCDIR=%~2"

REM Script is expected to be in the same directory as this BAT
set "SCRIPT=%~dp0New-OsfExfatImage.ps1"

if not exist "%SCRIPT%" (
  echo [ERROR] PowerShell script not found: "%SCRIPT%"
  echo Put New-OsfExfatImage.ps1 next to this .bat file.
  exit /b 2
)

if not exist "%SRCDIR%" (
  echo [ERROR] Source directory not found: "%SRCDIR%"
  exit /b 3
)

if not exist "%SRCDIR%\eboot.bin" (
  echo [ERROR] eboot.bin not found in source directory: "%SRCDIR%"
  exit /b 4
)

REM Run elevated? This BAT does not auto-elevate.
REM Right-click -> Run as administrator, or start cmd as admin.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -ImagePath "%IMAGE%" -SourceDir "%SRCDIR%" -ForceOverwrite

set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo [ERROR] Failed with exit code %RC%.
  exit /b %RC%
)

echo [OK] Done: "%IMAGE%"
exit /b 0

:usage
echo Usage:
echo   %~nx0 "C:\path\to\image.img" "C:\path\to\folder"
echo.
echo Notes:
echo   - Run this BAT as Administrator.
echo   - Image will be auto-sized.
exit /b 1
