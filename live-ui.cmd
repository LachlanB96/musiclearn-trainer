@echo off
rem live-ui on  : the plugin loads ui\ from this folder instead of its built-in copy, and reloads
rem               itself when index.html is saved. UI edits then need no build, install or re-added device.
rem live-ui off : back to the UI built into the plugin (what a release gives everyone else).
rem Takes effect the next time the plugin window opens. Progress is kept either way.
setlocal
set "CFG=%LOCALAPPDATA%\MusicLearnTrainer\ui-dir.txt"
if /i "%~1"=="on" (
    if not exist "%LOCALAPPDATA%\MusicLearnTrainer" mkdir "%LOCALAPPDATA%\MusicLearnTrainer"
    >"%CFG%" echo %~dp0ui
    echo Live UI on: the plugin loads %~dp0ui
    echo Close and reopen the plugin window once to switch over.
    exit /b 0
)
if /i "%~1"=="off" (
    if exist "%CFG%" del "%CFG%"
    echo Live UI off: the plugin uses the UI built into it. Rebuild ^(update.cmd^) to embed your latest edits.
    exit /b 0
)
if exist "%CFG%" ( echo Live UI is on: & type "%CFG%" ) else ( echo Live UI is off. )
echo Usage: live-ui on ^| off
