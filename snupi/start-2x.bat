@echo off
call "%~dp0start.bat" --resolution_scale=2 %*
exit /b %errorlevel%
