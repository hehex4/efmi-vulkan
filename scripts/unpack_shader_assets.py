"""Unpack verified local VFS bundles and inventory Shader objects without loading a game."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from xml.sax.saxutils import escape

from unity_shader_metadata import require

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('index', type=Path, help='Manifest produced by read_shader_vfs.py')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reader', type=Path, default=Path('F:/vulkan/unity-analysis/anime-reader'))
    parser.add_argument('--packages', type=Path, default=Path('F:/vulkan/unity-analysis/packages'))
    args = parser.parse_args()
    output = args.output.resolve()
    require(output.is_relative_to(ROOT / 'build'), 'Output must remain inside the project build directory')
    report = json.loads(args.index.read_text())
    require(report['payloads_checked'], 'Payload contents have not been checked')
    inputs = [Path(row['output']).resolve() for row in report['entries']]
    require(inputs and all(path.is_relative_to(ROOT / 'build') for path in inputs), 'Invalid local bundle paths')
    folders = {path.parent for path in inputs}
    require(len(folders) == 1, 'Expected one verified bundle directory')
    folder = next(iter(folders))
    require(set(folder.glob('*.ab')) == set(inputs), 'Unverified bundle present in the directory')
    for row, path in zip(report['entries'], inputs):
        require(digest(path) == row['sha256'], 'Extracted bundle hash changed')
    build = output / 'reader-build'
    build.mkdir(parents=True, exist_ok=True)
    source = escape(str(ROOT / 'scripts/unpack_shader_bundles.cs'), {'"': '&quot;'})
    project = build / 'reader.csproj'
    project.write_text(f'''<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net9.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><NuGetAudit>false</NuGetAudit><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup>
<ItemGroup><Compile Include="{source}" /></ItemGroup></Project>
''', encoding='utf-8')
    (build / 'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>', encoding='ascii')
    env = os.environ.copy()
    dependency = Path('F:/vulkan/unity-analysis')
    for key, value in dict(DOTNET_CLI_HOME='dotnet-home', NUGET_PACKAGES='nuget', TEMP='tmp', TMP='tmp').items():
        destination = dependency / value
        destination.mkdir(parents=True, exist_ok=True)
        env[key] = str(destination)
    env.update(DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_CLI_TELEMETRY_OPTOUT='1')
    with (output / 'build.log').open('w', encoding='utf-8') as log:
        subprocess.run(['dotnet', 'build', str(project), '--configuration', 'Release'], env=env,
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    assets = output / 'assets'
    with (output / 'unpack.log').open('w', encoding='utf-8') as log:
        subprocess.run(['dotnet', str(build / 'bin/Release/net9.0/reader.dll'), str(args.reader.resolve()),
                        str(folder), str(assets), str(ROOT / 'build')], env=env, cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    sys.path.insert(0, str(args.packages.resolve()))
    import UnityPy
    unpacked = json.loads((assets / 'manifest.json').read_text())
    total, selected = Counter(), []
    for row in unpacked['files']:
        path = Path(row['target']).resolve()
        require(path.is_relative_to(assets) and digest(path) == row['sha256'], 'Unpacked asset hash/path differs')
        env_assets = UnityPy.load(str(path))
        row['types'] = dict(Counter(obj.type.name for obj in env_assets.objects))
        total.update(row['types'])
        if row['types'].get('Shader'):
            selected.append(str(path))
    for row, path in zip(report['entries'], inputs):
        require(digest(path) == row['sha256'], 'Bundle changed during inspection')
    unpacked.update(total_types=dict(total), shader_assets=selected, source_index_sha256=digest(args.index))
    (output / 'inventory.json').write_text(json.dumps(unpacked, indent=2, ensure_ascii=True) + '\n', encoding='ascii')
    print('Inspected', len(inputs), 'bundles;', len(unpacked['files']), 'serialized assets;',
          total['Shader'], 'Shader objects in', len(selected), 'assets')


if __name__ == '__main__':
    main()
