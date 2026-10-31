@echo off
setlocal
pushd "%~dp0out\build\win-amd64-relwithdebinfo" || exit /b 1
snoopy.exe --license_mask=1 --no-fullscreen --render_target_path_d3d12=rtv --game_data_root "%~dp0..\decomp\extracted" --video_mode_refresh_rate=60 --vsync=true %*
set "result=%errorlevel%"
popd
exit /b %result%
