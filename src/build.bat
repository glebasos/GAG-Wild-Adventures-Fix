@echo off
rem Builds gagfx.dll (32-bit). Needs Visual Studio with the C++ x86 toolset.
setlocal
set VCVARS=
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VCVARS=%%i\VC\Auxiliary\Build\vcvars32.bat
if not exist "%VCVARS%" (echo Visual Studio C++ tools not found & exit /b 1)
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
if not exist ..\bin mkdir ..\bin
cl /nologo /O2 /GS- /MT /W3 /LD gagfix.cpp /Fe:..\bin\gagfx.dll /link user32.lib gdi32.lib kernel32.lib winmm.lib || exit /b 1
del /q gagfix.obj ..\bin\gagfx.exp ..\bin\gagfx.lib 2>nul
echo Built ..\bin\gagfx.dll
