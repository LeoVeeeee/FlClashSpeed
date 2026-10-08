from pathlib import Path
import hashlib
import re
import struct
import zipfile

root = Path(__file__).resolve().parents[1]
version = re.search(r'project\(FlClashSpeedPlugin\s+VERSION\s+([0-9.]+)',
                    (root / 'CMakeLists.txt').read_text(encoding='utf-8'))[1]
metadata_version = re.search(r'TMI_VERSION: return L"([0-9.]+)"',
                            (root / 'src' / 'Plugin.cpp').read_text(encoding='utf-8'))[1]
if version != metadata_version:
    raise RuntimeError('CMake and plugin metadata versions do not match')
output = root / 'dist' / 'release'
output.mkdir(parents=True, exist_ok=True)
if 'ALL TESTS PASSED' not in (root / 'test-results.txt').read_text(encoding='utf-8'):
    raise RuntimeError('A successful integration test report is required before packaging')
common = ['README.md', 'CHANGELOG.md', 'LICENSE', 'LICENSE.TrafficMonitor',
          'LICENSE.nlohmann-json', 'THIRD_PARTY_NOTICES.md']
required_licenses = ['Zig-MIT.txt', 'LLVM-libcxx.txt', 'LLVM-libcxxabi.txt',
                     'LLVM-libunwind.txt', 'MinGW-w64.txt']
for name in required_licenses:
    if not (root / 'LICENSES' / name).is_file():
        raise RuntimeError(f'Missing runtime license: {name}')
release = output / f'FlClashSpeed-TrafficMonitor-{version}.zip'
sources = output / f'FlClashSpeed-TrafficMonitor-{version}-source.zip'
hashes = []
with zipfile.ZipFile(release, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for arch, machine in [('x64', 0x8664), ('x86', 0x14c)]:
        path = root / 'dist' / arch / 'FlClashSpeedPlugin.dll'
        binary = path.read_bytes()
        if binary[:2] != b'MZ':
            raise RuntimeError('Not a Windows PE DLL')
        pe = struct.unpack_from('<I', binary, 60)[0]
        if binary[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', binary, pe+4)[0] != machine:
            raise RuntimeError(f'Wrong architecture: {arch}')
        archive.write(path, f'{arch}/FlClashSpeedPlugin.dll')
        hashes.append(f'{hashlib.sha256(binary).hexdigest()}  {arch}/FlClashSpeedPlugin.dll')
    for name in common:
        archive.write(root / name, name)
    for path in sorted((root / 'LICENSES').glob('*.txt')):
        archive.write(path, path.relative_to(root))
    archive.writestr('SHA256SUMS.txt', '\n'.join(hashes) + '\n')
    archive.write(root / 'test-results.txt', 'test-results.txt')
with zipfile.ZipFile(sources, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for name in common + ['CMakeLists.txt', '.gitignore', '.gitattributes',
                          'requirements-dev.txt', 'CONTRIBUTING.md']:
        archive.write(root / name, name)
    for directory in ['src', 'sdk', 'tools', 'tests', 'third_party', 'LICENSES', '.github']:
        for path in sorted((root / directory).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts:
                archive.write(path, path.relative_to(root))
    archive.write(root / 'docs' / 'CODE_REVIEW.md', 'docs/CODE_REVIEW.md')
checksums = output / 'SHA256SUMS.txt'
checksums.write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                           for p in (release, sources)), encoding='utf-8')
for path in (release, sources):
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise RuntimeError('Corrupt release archive')
    print(path)
print(checksums)
