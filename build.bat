@echo off
rem Сборка Grand Theft Completionist (один файл, Win32 + Direct2D)
rem Требуется zig c++ (https://ziglang.org/download)
if not exist C:\zig\zig.exe (
  echo [ERROR] C:\zig\zig.exe not found. Edit this file or install zig.
  exit /b 1
)
if not exist src\main.cpp (
  echo [ERROR] src\main.cpp not found
  exit /b 1
)
C:\zig\zig.exe c++ src\main.cpp -O2 -municode ^
  -ld2d1 -ldwrite -ldcomp -ld3d11 -ldxgi -lwinmm -lshlwapi ^
  -lole32 -lshell32 -ladvapi32 -luuid -lgdi32 -luser32 ^
  -o GrandTheftCompletionist.exe
if %errorlevel%==0 (
  echo [SUCCESS] GrandTheftCompletionist.exe created.
  echo Keep data and assets folders beside the EXE.
) else (
  echo [ERROR] Build failed.
)
