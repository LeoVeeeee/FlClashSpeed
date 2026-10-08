from pathlib import Path
import hashlib
import json
import requests
import zipfile
import shutil

root = Path(__file__).resolve().parents[1]
tools = root / "build-tools"
tools.mkdir(exist_ok=True)
index = requests.get("https://ziglang.org/download/index.json", timeout=30)
index.raise_for_status()
version = "0.15.2"
package = index.json()[version]["x86_64-windows"]
archive = tools / f"zig-{version}.zip"
if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != package["shasum"]:
    response = requests.get(package["tarball"], stream=True, timeout=(15, 60))
    response.raise_for_status()
    with archive.open("wb") as target:
        for chunk in response.iter_content(1024 * 1024):
            target.write(chunk)
if hashlib.sha256(archive.read_bytes()).hexdigest() != package["shasum"]:
    raise RuntimeError("Zig SHA256 mismatch")
with zipfile.ZipFile(archive) as z:
    for member in z.infolist():
        target = (tools / member.filename).resolve()
        if not target.is_relative_to(tools.resolve()):
            raise RuntimeError("Unsafe archive path")
    z.extractall(tools)
(tools / "zig-version.json").write_text(json.dumps({"version": version, **package}, indent=2), encoding="utf-8")
print("Verified Zig", version, flush=True)
destination = root / "third_party" / "json.hpp"
destination.parent.mkdir(exist_ok=True)
if not destination.exists():
    response = requests.get("https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp", timeout=30)
    response.raise_for_status()
    destination.write_bytes(response.content)
if hashlib.sha256(destination.read_bytes()).hexdigest() != "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63":
    raise RuntimeError("Vendored nlohmann/json SHA256 mismatch")
print("Verified nlohmann/json v3.12.0", flush=True)
license_sources = {
    "Zig-MIT.txt": "LICENSE",
    "LLVM-libcxx.txt": "lib/libcxx/LICENSE.TXT",
    "LLVM-libcxxabi.txt": "lib/libcxxabi/LICENSE.TXT",
    "LLVM-libunwind.txt": "lib/libunwind/LICENSE.TXT",
    "MinGW-w64.txt": "lib/libc/mingw/COPYING",
}
(root / "LICENSES").mkdir(exist_ok=True)
for name, source in license_sources.items():
    shutil.copyfile(tools / "zig-x86_64-windows-0.15.2" / source, root / "LICENSES" / name)
