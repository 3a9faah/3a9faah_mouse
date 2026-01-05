@echo off
chcp 65001 >nul 2>&1
echo ══════════════════════════════════════════════
echo         3a9faah Mouse Smoother Builder
echo ══════════════════════════════════════════════
echo.

set "GCC=C:\TDM-GCC-64\bin\g++"
if not exist "%GCC%" (
    set "GCC=g++"
)

echo [BUILD] Compiling with %GCC%...
"%GCC%" -O3 -march=native -mtune=native -flto -ffast-math ^
    -static -static-libgcc -static-libstdc++ ^
    -mwindows main.cpp -o 3a9faah_mouse.exe ^
    -luser32 -lkernel32 -lgdi32

if errorlevel 1 (
    echo.
    echo [FAILED] Build failed!
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Build complete: 3a9faah_mouse.exe
echo.
echo ══════════════════════════════════════════════
echo   Hotkeys:
echo   Ctrl+Shift+T = Toggle On/Off
echo   Ctrl+Shift+Q = Exit
echo   Ctrl+Shift+9 = Hide/Show
echo ══════════════════════════════════════════════
pause
