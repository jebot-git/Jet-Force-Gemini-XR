@echo off
rem Native OpenXR. ROM: Downloads\JetForceGemini.z64 or bin\roms\JetForceGemini.z64.
setlocal EnableExtensions
set "EXE=%~dp0bin\JetForceGeminiRecompiled.exe"
if not exist "%EXE%" ( echo Build the Windows XR target first. & exit /b 1 )
if "%~1"=="" (
  "%EXE%"
  exit /b
)
set "FIRST=%~1"
if "%FIRST:~0,2%"=="--" ("%EXE%" %*) else ("%EXE%" --rom "%~f1")
