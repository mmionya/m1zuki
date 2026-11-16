@echo off
if exist "%~dp0dist\m1zuki-windows\m1zuki.exe" (
  start "" "%~dp0dist\m1zuki-windows\m1zuki.exe" %*
) else (
  echo Run powershell -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1" -Bootstrap first.
  exit /b 1
)
