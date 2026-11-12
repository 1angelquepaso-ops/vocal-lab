@echo off
title MIRRORWALL capture - 2 SMOKE TEST
cd /d "%~dp0"
echo Loads every plugin once and does one short render (about 5-15 minutes).
echo To stop: press Ctrl+C in this window (not Task Manager).
echo.
set "PYEXE=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
"%PYEXE%" capture.py smoke
echo.
echo Done. Tell Claude.
pause
