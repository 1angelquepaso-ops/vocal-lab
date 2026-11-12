@echo off
title MIRRORWALL capture - 3 CAPTURE
cd /d "%~dp0"
echo Full captures. Claude tells you which group to run (or edit the word after capture.py capture).
echo To stop: press Ctrl+C in this window (not Task Manager). Run again to continue.
echo.
set "PYEXE=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
set "WHAT=%*"
if "%WHAT%"=="" set "WHAT=cla76"
"%PYEXE%" capture.py capture %WHAT%
echo.
echo Done. Tell Claude.
pause
