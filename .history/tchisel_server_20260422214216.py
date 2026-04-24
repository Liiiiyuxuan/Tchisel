#!/usr/bin/env python3
import json
import os
import re
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

BASE_DIR = Path(__file__).resolve().parent
EXECUTABLE = BASE_DIR / 'tchisel'
HTML_FILE = BASE_DIR / 'tchisel_local.html'
HOST = '127.0.0.1'
PORT = 8080


def find_executable() -> Path:
    candidates = [EXECUTABLE, BASE_DIR / 'solver', BASE_DIR / 'tchisla_solver']
    for c in candidates:
        if c.exists() and os.access(c, os.X_OK) and c.is_file():
            return c
    return EXECUTABLE


def parse_solver_output(text: str):
    """Parse the CLI output from ./tchisel <count> <digit> <target>."""
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if any('No solution found' in line for line in lines):
        return None

    formula = None
    for line in lines:
        if line.startswith('Found ') and 'solution' in line:
            continue
        formula = line
        break
    return formula


def plain_to_latex(expr: str) -> str:
    # Very small converter for common forms emitted by the solver.
    s = expr.strip()
    s = s.replace('*', r' \\times ')

    # pow(a,b) -> {a}^{b}, applied repeatedly.
    while 'pow(' in s:
        start = s.rfind('pow(')
        i = start + 4
        depth = 1
        comma = None
        while i < len(s):
            ch = s[i]
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0:
                    end = i
                    break
            elif ch == ',' and depth == 1 and comma is None:
                comma = i
            i += 1
        else:
            break
        if comma is None:
            break
        base = s[start + 4:comma]
        exp = s[comma + 1:end]
        repl = r'\left(' + base + r'\right)^{' + exp + '}'
        s = s[:start] + repl + s[end + 1:]

    # sqrt(x) -> \sqrt{x}, applied repeatedly.
    while 'sqrt(' in s:
        start = s.rfind('sqrt(')
        i = start + 5
        depth = 1
        while i < len(s):
            ch = s[i]
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0:
                    end = i
                    break
            i += 1
        else:
            break
        inner = s[start + 5:end]
        repl = r'\sqrt{' + inner + '}'
        s = s[:start] + repl + s[end + 1:]

    s = re.sub(r'(?<!\\)\(', r'\\left(', s)
    s = re.sub(r'(?<!\\)\)', r'\\right)', s)
    return s


class Handler(BaseHTTPRequestHandler):
    def _send(self, status: int, body: bytes, content_type: str = 'text/plain; charset=utf-8'):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self._send(204, b'')

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ('/', '/index.html'):
            if not HTML_FILE.exists():
                self._send(500, f'Missing HTML file: {HTML_FILE}'.encode())
                return
            self._send(200, HTML_FILE.read_bytes(), 'text/html; charset=utf-8')
            return

        if path == '/health':
            exe = find_executable()
            payload = {
                'ok': exe.exists() and os.access(exe, os.X_OK),
                'executable': str(exe),
                'html': str(HTML_FILE),
            }
            self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')
            return

        self._send(404, b'Not found')

    def do_POST(self):
        path = urlparse(self.path).path
        if path != '/solve':
            self._send(404, b'Not found')
            return

        try:
            length = int(self.headers.get('Content-Length', '0'))
            raw = self.rfile.read(length)
            data = json.loads(raw.decode('utf-8'))
        except Exception as e:
            self._send(400, json.dumps({'found': False, 'message': f'Bad JSON: {e}'}).encode(), 'application/json; charset=utf-8')
            return

        try:
            digit = int(data.get('digit'))
            target = int(data.get('target'))
            max_digits = int(data.get('maxDigits', 8))
        except Exception:
            self._send(400, json.dumps({'found': False, 'message': 'digit, target, and maxDigits must be integers.'}).encode(), 'application/json; charset=utf-8')
            return

        if digit < 1 or digit > 9 or max_digits <= 0:
            self._send(400, json.dumps({'found': False, 'message': 'digit must be 1..9 and maxDigits must be positive.'}).encode(), 'application/json; charset=utf-8')
            return

        exe = find_executable()
        if not exe.exists() or not exe.is_file():
            msg = f'Executable not found. Put your compiled solver at {exe.name} in the same folder as this server.'
            self._send(500, json.dumps({'found': False, 'message': msg}).encode(), 'application/json; charset=utf-8')
            return
        if not os.access(exe, os.X_OK):
            msg = f'File exists but is not executable: {exe}. Run chmod +x {exe.name} if needed.'
            self._send(500, json.dumps({'found': False, 'message': msg}).encode(), 'application/json; charset=utf-8')
            return

        stats = []
        found_formula = None
        found_digits = None

        for count in range(1, max_digits + 1):
            cmd = [str(exe), str(count), str(digit), str(target)]
            try:
                proc = subprocess.run(
                    cmd,
                    cwd=BASE_DIR,
                    capture_output=True,
                    text=True,
                    timeout=20,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                payload = {
                    'found': False,
                    'digits': count,
                    'stats': stats,
                    'message': f'Solver timed out at {count} digits.'
                }
                self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')
                return
            except Exception as e:
                payload = {'found': False, 'stats': stats, 'message': f'Failed to run solver: {e}'}
                self._send(500, json.dumps(payload).encode(), 'application/json; charset=utf-8')
                return

            combined = (proc.stdout or '') + '\n' + (proc.stderr or '')
            stats.append([count, 1])

            if proc.returncode != 0:
                payload = {
                    'found': False,
                    'digits': count,
                    'stats': stats,
                    'message': combined.strip() or f'Solver exited with code {proc.returncode}.'
                }
                self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')
                return

            formula = parse_solver_output(proc.stdout or '')
            if formula:
                found_formula = formula
                found_digits = count
                break

        if found_formula is None:
            payload = {
                'found': False,
                'digits': max_digits,
                'stats': stats,
                'message': f'No solution within {max_digits} digits.'
            }
            self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')
            return

        payload = {
            'found': True,
            'digits': found_digits,
            'formula': found_formula,
            'latex': plain_to_latex(found_formula),
            'stats': stats,
            'message': ''
        }
        self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')


def main():
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f'Serving on http://{HOST}:{PORT}')
    print(f'Using executable: {find_executable()}')
    print(f'Open http://{HOST}:{PORT}/ in your browser')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print('\nStopping server.')


if __name__ == '__main__':
    main()
