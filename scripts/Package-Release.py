"""Assemble the same verified Windows release locally and in GitHub Actions."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--build', default='build')
parser.add_argument('--output', default='dist')
parser.add_argument('--tag')
options = parser.parse_args()
root = Path(__file__).resolve().parent.parent
build = (root / options.build).resolve()
out = (root / options.output).resolve()
version = re.search(r'project\(NoiseBase VERSION ([0-9]+\.[0-9]+\.[0-9]+)', (root / 'CMakeLists.txt').read_text())[1]
if options.tag and options.tag != 'v' + version:
    raise SystemExit('Release tag must match the CMake project version.')
out.mkdir(parents=True, exist_ok=True)
stage = out / 'NoiseBase'
if stage.exists():
    raise SystemExit('Output staging directory already exists. Choose a fresh output directory.')
stage.mkdir()
shutil.copytree(build / 'NoiseBaseStandalone_artefacts/Release', stage / 'Standalone', ignore=shutil.ignore_patterns('*.pdb','*.ilk','*.lib','*.exp'))
shutil.copytree(build / 'NoiseBase_artefacts/Release/VST3/NoiseBase.vst3', stage / 'VST3/NoiseBase.vst3')
shutil.copytree(root / 'Licenses', stage / 'Licenses')
shutil.copytree(root / 'resources/generated', stage / 'Artwork')
for script in ('Install-NoiseBase.ps1','Release-Common.ps1'):
    shutil.copy2(root / 'scripts' / script, stage / script)
shutil.copy2(root / 'docs/Windows-user-guide.txt', stage / 'README.txt')
shutil.copy2(root / 'README.md', stage / 'README.md')
for source in (build / 'verification').rglob('*'):
    if not source.is_file():
        continue
    destination = stage / ('Previews' if source.suffix == '.png' else 'Verification') / source.relative_to(build / 'verification')
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source, destination)

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

manifest = {'schema': 1, 'product': 'studio.redweb.noisebase', 'repository': 'lakam99/NoiseBase',
            'version': version, 'platform': 'windows-x64', 'files': {}}
for directory in ('Standalone','VST3'):
    for path in sorted((stage / directory).rglob('*')):
        if path.is_file():
            manifest['files'][path.relative_to(stage).as_posix()] = digest(path)
(stage / 'release-manifest.json').write_text(json.dumps(manifest, indent=2))

source_files = subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
source_archive = out / f'NoiseBase-{version}-source.zip'
with zipfile.ZipFile(source_archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for name in sorted(filter(None, source_files)):
        path = root / name
        if path.is_file():
            assert path.name != 'google-services.json', 'Unused Firebase demo config must not be shipped.'
            archive.write(path, 'NoiseBase-source/' + name)
with zipfile.ZipFile(source_archive) as archive:
    assert archive.testzip() is None
shutil.copy2(source_archive, stage / 'NoiseBase-source.zip')
binary_archive = out / f'NoiseBase-{version}-Windows-x64.zip'
with zipfile.ZipFile(binary_archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            archive.write(path, 'NoiseBase/' + path.relative_to(stage).as_posix(), compress_type=zipfile.ZIP_STORED if path.suffix in ('.zip','.png') else zipfile.ZIP_DEFLATED)
with zipfile.ZipFile(binary_archive) as archive:
    assert archive.testzip() is None
(out / 'SHA256SUMS.txt').write_text(''.join(f'{digest(path)}  {path.name}\n' for path in (binary_archive, source_archive)))
print(json.dumps({'version':version, 'binary_archive':str(binary_archive), 'source_archive':str(source_archive)}))
