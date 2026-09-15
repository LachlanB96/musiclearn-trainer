"""package.py - zip the built VST3 for distribution.

Run build.cmd first. Writes dist/MusicLearn-Trainer-<version>-win64.zip containing the
"MusicLearn Trainer.vst3" folder from the build output plus an INSTALL.txt, and skips the
*.old* copies that install.ps1 leaves behind when Live has the plugin loaded. The version
comes from the built moduleinfo.json, so the zip name always matches the binary.

    python package.py            # -> dist/MusicLearn-Trainer-0.2.0-win64.zip
    gh release create v0.2.0 dist/MusicLearn-Trainer-0.2.0-win64.zip --title "MusicLearn Trainer 0.2.0" --notes-file notes.md
"""
import os, re, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(os.environ['LOCALAPPDATA'], 'MusicLearnTrainer', 'build', 'MusicLearnTrainer_artefacts', 'Release', 'VST3', 'MusicLearn Trainer.vst3')
DIST = os.path.join(HERE, 'dist')

INSTALL = """MusicLearn Trainer {version} - Windows VST3 (64-bit)

1. If Windows flagged this zip as downloaded, right-click it, Properties, tick Unblock, OK, then extract.
2. Copy the folder "MusicLearn Trainer.vst3" (the whole folder) into
       C:\\Program Files\\Common Files\\VST3\\
   (you will be asked for administrator permission).
3. In Ableton Live: Options > Preferences > Plug-Ins, turn on "Use VST3 System Folders", then Rescan.
   In the browser go to Plug-Ins > VST3 > MusicLearn > MusicLearn Trainer and drag it onto a MIDI track.
   Set that track's MIDI From to your keyboard and Monitor to In. Click the wrench on the device to open it.

Needs Windows 10 or 11 with the WebView2 runtime (built into Windows 11; comes with Edge on Windows 10),
and the Microsoft Visual C++ 2015-2022 runtime (most machines have it; if the plugin does not appear after
a rescan, install "vc_redist.x64.exe" from Microsoft and rescan again).

Setup, modes and the play-along guide are described at https://github.com/LachlanB96/musiclearn-trainer
Licence: GNU AGPL v3. The Bravura font is (c) Steinberg Media Technologies, SIL Open Font License 1.1.
"""


def main():
    info = os.path.join(ART, 'Contents', 'Resources', 'moduleinfo.json')
    if not os.path.exists(info):
        raise SystemExit(f'Build not found at {ART} - run build.cmd first')
    m = re.search(r'"Version"\s*:\s*"([^"]+)"', open(info, encoding='utf-8').read())   # moduleinfo allows trailing commas, so no json.load
    version = m.group(1) if m else 'unknown'
    os.makedirs(DIST, exist_ok=True)
    out = os.path.join(DIST, f'MusicLearn-Trainer-{version}-win64.zip')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr('INSTALL.txt', INSTALL.format(version=version))
        for root, _dirs, files in os.walk(ART):
            for f in files:
                if '.old' in f:
                    continue
                p = os.path.join(root, f)
                z.write(p, os.path.relpath(p, os.path.dirname(ART)).replace(os.sep, '/'))
    print(f'{out}  ({os.path.getsize(out) / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()
