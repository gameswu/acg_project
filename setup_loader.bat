@echo off
REM Setup Python loader environment
REM This script creates a virtual environment with Python 3.11.14 and installs dependencies

echo ========================================
echo ACG Project - Scene Loader Setup
echo ========================================
echo.

REM Check if uv is installed
where uv >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] uv not found. Please install uv first:
    echo   https://github.com/astral-sh/uv
    echo   Or run: pip install uv
    pause
    exit /b 1
)

REM Check if loader directory exists
REM Support running from project root or bin/ directory
if exist "loader" (
    set "LOADER_DIR=loader"
    echo Running from project root directory
) else if exist "..\loader" (
    set "LOADER_DIR=..\loader"
    echo Running from bin directory
    cd ..
) else (
    echo [ERROR] loader directory not found!
    echo Please run this script from either:
    echo   - Project root directory (where loader/ exists^)
    echo   - bin/ directory (where the executable is^)
    pause
    exit /b 1
)

REM Check if Python 3.11.14 is available
echo Checking for Python 3.11.14...
uv python list | findstr "3.11.14" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Python 3.11.14 not found, installing...
    uv python install 3.11.14
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Failed to install Python 3.11.14
        pause
        exit /b 1
    )
)

REM Create virtual environment
echo.
echo Creating virtual environment with Python 3.11.14...
if exist "%LOADER_DIR%\.venv" (
    echo Virtual environment already exists, removing...
    rmdir /s /q "%LOADER_DIR%\.venv"
)

cd %LOADER_DIR%
uv venv --python 3.11.14
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to create virtual environment
    cd ..
    pause
    exit /b 1
)

REM Install dependencies
echo.
echo Installing dependencies...
uv pip install -r requirements.txt
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to install dependencies
    cd ..
    pause
    exit /b 1
)

cd ..

echo ========================================
echo Setup completed successfully!
echo ========================================
echo.
echo The loader is now ready to use.
echo Virtual environment: %LOADER_DIR%\.venv
echo.
echo Run CMake build to copy loader to bin/ directory.
echo Then the renderer will automatically use it.
echo.
pause
