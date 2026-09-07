# Runs elevated (launched by install.cmd). Copies the built VST3 into the system VST3 folder.
# If Live has the plugin loaded the DLL is locked: it cannot be deleted or overwritten, but it can
# be renamed, so the old binary is moved aside and cleaned up on a later install.
$ErrorActionPreference = 'Stop'
$log = Join-Path $env:LOCALAPPDATA 'MusicLearnTrainer\install.log'
$src = Join-Path $env:LOCALAPPDATA 'MusicLearnTrainer\build\MusicLearnTrainer_artefacts\Release\VST3\MusicLearn Trainer.vst3'
$dst = 'C:\Program Files\Common Files\VST3\MusicLearn Trainer.vst3'
$binDir = Join-Path $dst 'Contents\x86_64-win'
$bin = Join-Path $binDir 'MusicLearn Trainer.vst3'
$out = @()
try {
    if (-not (Test-Path $src)) { throw "Build not found at $src - run build.cmd first" }
    # remove leftovers from earlier installs that are no longer locked
    if (Test-Path $binDir) {
        Get-ChildItem $binDir -Filter '*.old*' | ForEach-Object { $f = $_; try { Remove-Item $f.FullName -Force; $out += "removed old $($f.Name)" } catch { $out += "still locked (Live still has it loaded): $($f.Name)" } }
    }
    if (Test-Path $bin) {
        try { Remove-Item $bin -Force; $out += 'replaced existing binary' }
        catch {
            $aside = 'MusicLearn Trainer.vst3.old' + (Get-Date -Format 'yyyyMMddHHmmss')
            Rename-Item -Path $bin -NewName $aside
            $out += "binary was in use (Live open?) - moved aside as $aside; Live uses the new build next time the plugin loads"
        }
    }
    # a nested copy left behind by an earlier attempt would shadow nothing but is confusing - remove it
    $nested = Join-Path $dst 'MusicLearn Trainer.vst3'
    if (Test-Path $nested -PathType Container) { Remove-Item -Recurse -Force $nested }
    New-Item -ItemType Directory -Force $dst | Out-Null
    Copy-Item -Path (Join-Path $src '*') -Destination $dst -Recurse -Force   # merge contents, never nest
    $out += "installed $((Get-Item $bin).LastWriteTime) $((Get-Item $bin).Length) bytes -> $dst"
    $out += 'OK'
} catch {
    $out += "FAILED: $($_.Exception.Message)"
}
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
$out | Set-Content -Path $log
