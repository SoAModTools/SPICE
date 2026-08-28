@echo off
setlocal enableextensions enabledelayedexpansion

set "SPICE_GRINDER=%~1"
set "INPUT_DIR=%~2"
set "OUTPUT_DIR=%~3"

if "%SPICE_GRINDER%"=="" goto :usage
if "%INPUT_DIR%"=="" goto :usage
if "%OUTPUT_DIR%"=="" goto :usage

if not exist "%SPICE_GRINDER%" (
  echo [run_ab] ERROR: SpiceGrinder.exe not found at "%SPICE_GRINDER%"
  echo [run_ab] Usage:
  echo [run_ab]   run_ab.bat SpiceGrinder.exe input_dir output_dir
  exit /b 1
)

echo [run_ab] "%SPICE_GRINDER%" compare-mld-sa3d --input "%INPUT_DIR%" --output "%OUTPUT_DIR%"
"%SPICE_GRINDER%" compare-mld-sa3d --input "%INPUT_DIR%" --output "%OUTPUT_DIR%"
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
  echo [run_ab] FAILED with exit code %EXIT_CODE%
  exit /b %EXIT_CODE%
)

echo [run_ab] Completed successfully.
exit /b 0

:usage
echo [run_ab] Usage:
echo [run_ab]   run_ab.bat SpiceGrinder.exe input_dir output_dir
exit /b 2
