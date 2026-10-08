@echo off
setlocal
cd /d "%~dp0"

echo === Configuring ===
cmake -B build -G "Visual Studio 17 2022" -A x64 || goto :fail

echo === Building Release ===
cmake --build build --config Release || goto :fail

set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
    echo.
    echo Inno Setup 6 not found. Install it from https://jrsoftware.org/isdl.php and run this again.
    goto :fail
)

echo === Building installer ===
"%ISCC%" installer\SigmaQ.iss || goto :fail

echo.
echo Done! Installer: installer\output\SigmaQ-Setup-0.1.0.exe
exit /b 0

:fail
echo.
echo Build failed - see messages above.
exit /b 1
