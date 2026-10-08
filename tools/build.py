from pathlib import Path
import argparse
import subprocess
import os

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--arch", choices=("x64", "x86", "all"), default="x64")
args = parser.parse_args()
zig = root / "build-tools" / "zig-x86_64-windows-0.15.2" / "zig.exe"
if not zig.exists():
    raise SystemExit("Run tools/bootstrap.py first")
environment = os.environ.copy()
environment["ZIG_GLOBAL_CACHE_DIR"] = str(root / "build" / "global-cache")
environment["ZIG_LOCAL_CACHE_DIR"] = str(root / "build" / "local-cache")
for arch in (("x64", "x86") if args.arch == "all" else (args.arch,)):
    output = root / "dist" / arch
    output.mkdir(parents=True, exist_ok=True)
    target = "x86_64-windows-gnu" if arch == "x64" else "x86-windows-gnu"
    subprocess.run([
        str(zig), "c++", "-target", target, "-std=c++17", "-O2", "-shared", "-s",
        "-DUNICODE", "-D_UNICODE",
        "src/Core.cpp", "src/Runtime.cpp", "src/Options.cpp", "src/Plugin.cpp",
        "-lwinhttp", "-lcrypt32", "-lcomdlg32", "-luser32", "-lshell32",
        "-o", str(output / "FlClashSpeedPlugin.dll")
    ], cwd=root, env=environment, check=True)
    binary = (output / "FlClashSpeedPlugin.dll").read_bytes()
    if not binary.startswith(b"MZ"):
        raise RuntimeError("Compiler did not produce a Windows PE DLL")
    print("Built", output / "FlClashSpeedPlugin.dll", flush=True)
    test_output = root / "build" / arch
    test_output.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(zig), "c++", "-target", target, "-std=c++17", "-O2", "-static", "-municode",
        "-DUNICODE", "-D_UNICODE", "tests/PluginHost.cpp", "src/Core.cpp",
        "-lwinhttp", "-lcrypt32", "-lshell32", "-o", str(test_output / "PluginCheck.exe")
    ], cwd=root, env=environment, check=True)
