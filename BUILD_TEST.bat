@echo off
REM BUILD_TEST.bat - Test compilation with EXTREME PRECISION
REM Every error is a failure of our psychotic precision

echo ========================================
echo AARENDOCOREGLM - FOUNDATION BUILD TEST
echo ========================================
echo.

REM Find MSBuild
set MSBUILD_PATH=
for /f "delims=" %%i in ('where msbuild 2^>nul') do set MSBUILD_PATH=%%i

if "%MSBUILD_PATH%"=="" (
    echo ERROR: MSBuild not found in PATH
    echo Please run from Developer Command Prompt for Visual Studio
    exit /b 1
)

echo Found MSBuild: %MSBUILD_PATH%
echo.

echo Building Debug configuration...
"%MSBUILD_PATH%" AARendoCoreGLM.sln /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /m

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo FAILURE: Debug build failed with errors
    echo ========================================
    exit /b 1
)

echo.
echo Building Release configuration...
"%MSBUILD_PATH%" AARendoCoreGLM.sln /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ========================================
    echo FAILURE: Release build failed with errors
    echo ========================================
    exit /b 1
)

echo.
echo ========================================
echo SUCCESS: Both Debug and Release built with ZERO ERRORS
echo ========================================
echo.
echo Output files:
dir /b bin\Debug\*.dll 2>nul
dir /b bin\Release\*.dll 2>nul