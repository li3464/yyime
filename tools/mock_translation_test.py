"""Exercise WinHTTP against a loopback API. Never uses the real API or user config."""
import json
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path
import subprocess
import tempfile
import threading
import sys

root = Path(__file__).resolve().parent.parent
requests = []
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        requests.append((self.path, request, self.headers.get('Authorization')))
        body = json.dumps({'choices': [{'message': {'content': json.dumps({'candidates': ['Hello, world!']})}}]}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    with tempfile.TemporaryDirectory(prefix='yyime-regression-', dir=root/'build') as user:
        result = subprocess.run([str(root/'build/regression_test.exe'), str(root/'data'), user,
                                 f'http://127.0.0.1:{server.server_port}/v1'], check=False)
    if result.returncode == 0 and '--host' in sys.argv:
        for arch in ('x64', 'x86'):
            result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                str(root/'tools/run_host_test.ps1'), '-Arch', arch, '-MockBaseUrl',
                f'http://127.0.0.1:{server.server_port}/v1'], check=False)
            if result.returncode: break
    server.shutdown()
if result.returncode:
    print(f'Mock server received {len(requests)} requests')
    raise SystemExit(result.returncode)
assert len(requests) == (3 if '--host' in sys.argv else 1), len(requests)
for path, request, auth in requests:
    assert path == '/v1/chat/completions', path
    assert auth == 'Bearer fake-test-only'
    assert request['messages'][1]['content'] == '你好世界'
    assert request['response_format']['type'] == 'json_object'
print('PASS mock HTTP path, auth, selected text, response format; no external API used')
