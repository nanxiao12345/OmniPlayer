@echo off
setlocal enabledelayedexpansion

echo ============================================
echo  MediaPlayer Build Script
echo ============================================

set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "QT_PATH=E:\DevTool\QT\6.11.0\msvc2022_64"
set "FFMPEG_PATH=D:\Tools\ffmpeg-shared"
set "PROJECT_DIR=F:\OmniPlayer"
set "BUILD_DIR=%PROJECT_DIR%\build"
set "OUTPUT_DIR=%BUILD_DIR%\src\Release"

echo [1/5] Setting up MSVC environment...
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to set up MSVC environment
    pause
    exit /b 1
)
echo MSVC environment ready.

echo [2/5] Configuring CMake...
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_PATH%" ^
    -DFFMPEG_ROOT="%FFMPEG_PATH%"
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed
    pause
    exit /b 1
)
echo CMake configuration done.

echo [3/5] Building Release...
cmake --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)
echo Build succeeded.

echo [4/5] Deploying Qt dependencies...
if exist "%OUTPUT_DIR%\MediaPlayer.exe" (
    "%QT_PATH%\bin\windeployqt.exe" "%OUTPUT_DIR%\MediaPlayer.exe" --release --no-compiler-runtime --no-translations
    echo Qt deployment done.
) else (
    echo WARNING: MediaPlayer.exe not found in %OUTPUT_DIR%
)

echo [5/5] Copying FFmpeg DLLs...
if exist "%FFMPEG_PATH%\bin\avcodec-62.dll" (
    copy /Y "%FFMPEG_PATH%\bin\avcodec-62.dll" "%OUTPUT_DIR%\" > nul
    copy /Y "%FFMPEG_PATH%\bin\avformat-62.dll" "%OUTPUT_DIR%\" > nul
    copy /Y "%FFMPEG_PATH%\bin\avfilter-11.dll" "%OUTPUT_DIR%\" > nul
    copy /Y "%FFMPEG_PATH%\bin\avutil-60.dll" "%OUTPUT_DIR%\" > nul
    copy /Y "%FFMPEG_PATH%\bin\swresample-6.dll" "%OUTPUT_DIR%\" > nul
    copy /Y "%FFMPEG_PATH%\bin\swscale-9.dll" "%OUTPUT_DIR%\" > nul
    echo FFmpeg 62.x DLLs copied.
) else (
    echo WARNING: FFmpeg 62.x DLLs not found at %FFMPEG_PATH%\bin\
)

echo ============================================
echo  Build Complete!
echo  Output: %OUTPUT_DIR%\MediaPlayer.exe
echo ============================================

endlocal
