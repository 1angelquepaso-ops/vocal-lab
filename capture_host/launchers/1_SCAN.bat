@echo off
title MIRRORWALL capture - 1 SCAN (once)
cd /d "%~dp0"
echo Reads your Waves plugins ONCE and saves the list (2-5 minutes).
echo If a Waves window pops up (Grand Rhapsody etc.), click Cancel yourself.
echo.
set "PYEXE=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
"%PYEXE%" -m pip install --disable-pip-version-check -q -U numpy soundfile >> pip_log.txt 2>&1
"%PYEXE%" capture.py scan
echo.
echo Done. Tell Claude.
pause
