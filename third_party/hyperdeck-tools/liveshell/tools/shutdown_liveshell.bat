@echo off
rem Shut LiveShell Engine down on the PS5 (same as "Shut down LiveShell" in the app's Settings).
rem Usage: shutdown_liveshell.bat [PS5 IP address]
setlocal
set HOST=%~1
if "%HOST%"=="" set /p HOST=PS5 IP address (shown in LiveShell Engine at the top right):
if "%HOST%"=="" exit /b 1
echo Shutting down LiveShell Engine on %HOST%...
powershell -NoProfile -Command "$c = New-Object Net.Sockets.TcpClient; try { $c.Connect('%HOST%', 9049); $r = New-Object IO.StreamReader($c.GetStream()); Write-Host ('LiveShell Engine: ' + $r.ReadLine()); $c.Close() } catch { Write-Host 'LiveShell Engine is not running on that address (port 9049 closed).' }"
echo Done.
pause
