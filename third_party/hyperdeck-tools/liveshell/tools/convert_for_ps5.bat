@echo off
rem LiveShell Engine video converter. Drag one or more videos onto this file.
rem Any format in (mkv, mov, avi, webm, mp4, 4K, 120 fps ...) -> <name>_ps5.mp4 next to the original:
rem H.264, 1920x1080 (smaller videos are fitted with black bars), max 60 fps; the sound is kept as it is (AAC).
rem Needs ffmpeg.exe in this folder (included in the release) or on PATH.
setlocal
set FFMPEG=%~dp0ffmpeg.exe
if not exist "%FFMPEG%" set FFMPEG=ffmpeg
"%FFMPEG%" -version >nul 2>&1
if errorlevel 1 (
  echo ffmpeg.exe was not found. Keep it in the same folder as this file.
  pause
  exit /b 1
)
if "%~1"=="" (
  echo Drag one or more video files onto convert_for_ps5.bat.
  pause
  exit /b 1
)

:next
if "%~1"=="" goto done
echo.
echo === %~nx1
"%FFMPEG%" -hide_banner -loglevel error -stats -y -i "%~1" ^
  -vf "scale=1920:1080:force_original_aspect_ratio=decrease:flags=lanczos,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,format=yuv420p" ^
  -fpsmax 60 -c:v libx264 -preset slow -profile:v high -level 4.2 -b:v 16M -maxrate 20M -bufsize 20M ^
  -x264-params "keyint=60:min-keyint=30:scenecut=0:open-gop=0" ^
  -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv ^
  -c:a aac -b:a 192k -ar 48000 -ac 2 ^
  -movflags +faststart "%~dpn1_ps5.mp4"
if errorlevel 1 (echo FAILED: %~nx1) else (echo OK: %~n1_ps5.mp4)
shift
goto next

:done
echo.
echo Done. Copy the _ps5.mp4 files to /data/LiveShell/video on the PS5, to a "liveshell"
echo folder on a USB drive, or upload them on the LiveShell Engine phone / PC page.
pause
