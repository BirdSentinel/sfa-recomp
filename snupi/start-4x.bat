@echo off
call "%~dp0start.bat" --resolution_scale=4 %*
exit /b %errorlevel%
