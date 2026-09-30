@echo off
REM บิ้วปลั๊กอิน + ตัวติดตั้ง ในเครื่องตัวเอง
REM ต้องมี: Visual Studio 2022, CMake, Inno Setup 6
setlocal
cd /d "%~dp0"

echo [1/3] configure
cmake -B build -G "Visual Studio 17 2022" -A x64 || goto :fail

echo [2/3] build plugin
cmake --build build --config Release --target DeFeedback_VST3 || goto :fail

echo [3/3] build installer
set ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" set ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" (
  echo ไม่พบ Inno Setup 6 - ติดตั้งจาก https://jrsoftware.org/isdl.php
  goto :fail
)
"%ISCC%" installer\DeFeedback.iss || goto :fail

echo.
echo เสร็จแล้ว ไฟล์ติดตั้งอยู่ที่ installer\Output\
dir /b installer\Output\*.exe
goto :eof

:fail
echo.
echo บิ้วไม่ผ่าน
exit /b 1
