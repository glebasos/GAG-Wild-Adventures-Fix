@echo off
rem GAG Modern - double-click installer. Add -Shortcut to also create a desktop shortcut.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
pause
