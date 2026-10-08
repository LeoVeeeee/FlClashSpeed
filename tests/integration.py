from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import subprocess
import threading
import tempfile
import time

root = Path(__file__).resolve().parents[1]
report = []
seen = []
mode = 'normal'

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *_): pass
    def do_GET(self):
        self.close_connection = True
        attempt = len(seen)
        seen.append((self.path, self.headers.get("Authorization")))
        try:
            if attempt == 0:
                self.send_response(401)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if mode == 'redirect':
                self.send_response(302)
                self.send_header('Location', f'http://127.0.0.1:{self.server.server_port}/should-not-follow')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Connection", "close")
            self.end_headers()
            if mode == 'malformed':
                self.wfile.write(b'{secret-canary}\n')
                self.wfile.flush()
                return
            if mode == 'trickle':
                for _ in range(16):
                    self.wfile.write(b'{')
                    self.wfile.flush()
                    time.sleep(.5)
                return
            if attempt == 1:
                # Exercise records split between HTTP reads and multiple records per read.
                self.wfile.write(b'{"up":126412,')
                self.wfile.flush()
                time.sleep(.15)
                self.wfile.write(b'"down":1572864}\n{"up":126412,"down":1572864}\n')
                self.wfile.flush()
                time.sleep(7)  # Stalled stream must become disconnected and reconnect.
            else:
                for _ in range(15):
                    self.wfile.write(b'{"up":0,"down":0}\n')
                    self.wfile.flush()
                    time.sleep(.5)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

def check(condition, description):
    if not condition:
        raise AssertionError(description)
    report.append("PASS: " + description)
    print(report[-1], flush=True)

def execute(arch, directory, seconds, *arguments):
    # Sampling starts after loading/DPAPI checks. Allow cold WOW64 startup
    # separately; the host's 100 ms refresh/reconnect limit remains unchanged.
    command = [str(root / "build" / arch / "PluginCheck.exe"),
               str(root / "dist" / arch / "FlClashSpeedPlugin.dll"),
               str(directory), str(seconds), *arguments]
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", timeout=seconds + 30)
    except subprocess.TimeoutExpired as error:
        partial = error.stdout or b''
        if isinstance(partial, bytes): partial = partial.decode('utf-8', errors='replace')
        (root / 'build' / f'{arch}-timeout.jsonl').write_text(partial, encoding='utf-8')
        raise AssertionError(f'{arch} test host exceeded {seconds + 30}s; last output: {partial[-1500:]}') from error
    (root / "build" / f"{arch}-stream-test.jsonl").write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise AssertionError(f"{arch} host exit {result.returncode}: {result.stderr} {result.stdout[:500]}")
    check(True, f"{arch} DLL exports, API 7, metadata, format boundaries, UTF-16/DPAPI/atomic settings and shutdown")
    return [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]

def main():
    global mode
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="plugin-test-", dir=root / "build") as fixture:
        folder = Path(fixture)
        (folder / "config.yaml").write_text('external-controller: ""\nsecret: "test-token"\n', encoding="utf-8")
        (folder / "shared_preferences.json").write_text(json.dumps({"flutter.config": json.dumps({"patchClashConfig": {
            "external-controller": f"127.0.0.1:{server.server_port}"
        }})}), encoding="utf-8")
        (folder / "FlClashSpeedPlugin.ini").write_text(f"[connection]\nconfig_path={folder / 'config.yaml'}\n", encoding="utf-16")
        rows = execute("x64", folder, 17)
        statuses = [row["status"] for row in rows]
        check(any("密钥不匹配" in status for status in statuses), "401 reports authentication failure")
        check(any(row["up"] == "123.45 KB/s" and row["down"] == "1.50 MB/s" for row in rows), "stale YAML GUI override, stream fragmentation and speed formatting")
        check(any("超时" in status for status in statuses), "stalled stream becomes disconnected")
        check(any(row["up"] == "0.00 KB/s" and row["down"] == "0.00 KB/s" for row in rows), "automatic recovery and real idle speed")
        check(all(path == "/traffic" and auth == "Bearer test-token" for path, auth in seen), "read-only local authenticated traffic requests")
        check(max(row["call_ms"] for row in rows) < 100, "host refresh never waits for HTTP")
        rows = execute("x86", folder, 3)
        check(any(row["up"] == "0.00 KB/s" for row in rows), "32-bit DLL streams and renders item values")
        attempts = len(seen)
        rows = execute("x64", folder, 6, "--reconnect")
        check(len(seen) >= attempts + 2 and any(row['up'] == '0.00 KB/s' for row in rows), "rapid manual reconnect commands are non-blocking and recover")
        mode = 'redirect'
        attempts = len(seen)
        rows = execute('x64', folder, 1)
        check(any('控制器返回错误' in row['status'] for row in rows) and all(path == '/traffic' for path, _ in seen[attempts:]), 'HTTP redirects are rejected without forwarding credentials')
        mode = 'malformed'
        rows = execute('x64', folder, 1)
        check(any('有效 JSON' in row['status'] for row in rows) and all('secret-canary' not in row['status'] for row in rows), 'malformed traffic does not echo response payload')
        mode = 'trickle'
        rows = execute('x64', folder, 6)
        check(any('超时' in row['status'] for row in rows), 'continuous incomplete fragments still expire')
        mode = 'normal'
        attempts = len(seen)
        (folder / "FlClashSpeedPlugin.ini").write_text("[connection]\nsecret_dpapi=z\n", encoding="utf-16")
        rows = execute("x64", folder, 1)
        check(len(seen) == attempts and any("损坏" in row['status'] for row in rows), "corrupt credentials do not silently fall back or make network requests")
        (folder / "FlClashSpeedPlugin.ini").write_text("[connection]\nendpoint=http://example.com:9090\n", encoding="utf-16")
        rows = execute("x64", folder, 1)
        check(any("只允许连接本机" in row["status"] for row in rows), "remote controllers are rejected before networking")
    server.shutdown()
    report.append("ALL TESTS PASSED")

try:
    main()
except Exception as error:
    report.append("FAIL: " + repr(error))
    raise
finally:
    (root / "test-results.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
