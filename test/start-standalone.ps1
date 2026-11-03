#Requires -Version 7
<#
Opens the Trainer's Standalone for test/smoke.ts and prints its process id. It starts through Start-Unfocused
(LBTerm's, the file -StartUnfocused names), so it comes up without taking focus and inside the warden job the smoke
test runs in, and its window is then moved onto the right-most screen, still without activating it.

  -Exe   the Standalone to open
  -Data  the folder it keeps its WebView2 profile in for this run (MUSICLEARN_TRAINER_DATA), so the run neither
         reads nor changes the real one and loads the UI built into it, whatever live-ui.cmd says
  -Port  WebView2's remote debugging port, which smoke.ts asks the page through
#>
param(
  [Parameter(Mandatory)][string]$StartUnfocused,
  [Parameter(Mandatory)][string]$Exe,
  [Parameter(Mandatory)][string]$Data,
  [Parameter(Mandatory)][int]$Port
)
$ErrorActionPreference = 'Stop'
. $StartUnfocused

Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TrainerSmokeWindow {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int w, int h, uint flags);
}
'@
# Screen bounds and SetWindowPos both in physical pixels
[TrainerSmokeWindow]::SetProcessDPIAware() | Out-Null

$id = Start-Unfocused -Exe $Exe -WorkingDirectory (Split-Path $Exe) -Job -Environment @{
  MUSICLEARN_TRAINER_DATA               = $Data
  WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = "--remote-debugging-port=$Port"
}
$hwnd = Wait-Window -Id $id -Seconds 15
if ($hwnd -eq [IntPtr]::Zero) {
  Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
  throw "the Standalone (pid $id) showed no window in 15 s"
}

$screen = [System.Windows.Forms.Screen]::AllScreens | Sort-Object { $_.Bounds.Right } | Select-Object -Last 1
# SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
if (-not [TrainerSmokeWindow]::SetWindowPos($hwnd, [IntPtr]::Zero, $screen.Bounds.X + 20, $screen.Bounds.Y + 60, 0, 0, 0x15)) {
  Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
  throw "couldn't move the Standalone's window onto $($screen.DeviceName)"
}
$id
