@echo off
rem Build and install in one go (one UAC prompt). Only needed after changes to Source\ (the C++),
rem or to embed the current UI for a release; with live-ui on, UI edits show up without this.
call "%~dp0build.cmd" || exit /b 1
call "%~dp0install.cmd" || exit /b 1
echo.
echo Installed. In Live: remove and re-add the Trainer device (or reopen the set) to load the new build.
