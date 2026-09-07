@echo off
rem Copies the built VST3 into the system VST3 folder (asks for admin via UAC).
rem Run build.cmd first. Then in Ableton: reload the plugin (or Preferences > Plug-Ins > Rescan).
set "PS1=%~dp0install.ps1"
set "LOG=%LOCALAPPDATA%\MusicLearnTrainer\install.log"
if exist "%LOG%" del "%LOG%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-WindowStyle','Hidden','-File',('\"' + $env:PS1 + '\"'))"
if not exist "%LOG%" ( echo Install did not run - UAC declined? & exit /b 1 )
type "%LOG%"
findstr /c:"OK" "%LOG%" >nul || exit /b 1
