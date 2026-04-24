#!/usr/bin/env python3
import json
import math
import os
import re
import signal
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

BASE_DIR = Path(__file__).resolve().parent
EXECUTABLE = BASE_DIR / 'tchisel'
HTML_FILE = BASE_DIR / 'main.html'
HOST = '127.0.0.1'
PORT = 8080
EPS = 1e-9

# Track the current solver subprocess so /cancel can kill it
_proc_lock = threading.Lock()
_current_proc = None


def _kill_proc(proc):
    """Kill a subprocess and its entire process group."""
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except (OSError, ProcessLookupError):
        pass
    try:
        proc.kill()
    except (OSError, ProcessLookupError):
        pass
    try:
        proc.wait(timeout=3)
    except Exception:
        pass

ANSI_RE = re.compile(r'\x1B\[[0-?]*[ -/]*[@-~]')
STATUS_LINE_RE = [
    re.compile(r'^found\s+\d+\s+solution', re.I),
    re.compile(r'^solved\s+in\s+\d+\s+digit\(s\)!?$', re.I),
    re.compile(r'^solution\s+found', re.I),
    re.compile(r'^usage:', re.I),
    re.compile(r'^no\s+solution\s+found', re.I),
]


def find_executable() -> Path:
    candidates = [EXECUTABLE, BASE_DIR / 'solver', BASE_DIR / 'tchisla_solver']
    for c in candidates:
        if c.exists() and os.access(c, os.X_OK) and c.is_file():
            return c
    return EXECUTABLE


def clean_solver_lines(text: str):
    text = ANSI_RE.sub('', text or '')
    return [line.strip() for line in text.splitlines() if line.strip()]


def looks_like_formula(line: str) -> bool:
    if not line:
        return False
    if any(rx.search(line) for rx in STATUS_LINE_RE):
        return False
    if re.fullmatch(r'-?\d+(?:\.\d+)?', line):
        return True
    return any(tok in line for tok in ('pow(', 'sqrt(', '+', '-', '*', '/', '^', '(', ')', '!', '='))


def extract_formula_candidates(text: str):
    lines = clean_solver_lines(text)
    out = []
    for line in lines:
        if not looks_like_formula(line):
            continue
        # If line looks like "4 = expr", keep just expr when left side is a plain number.
        m = re.match(r'^\s*-?\d+(?:\.\d+)?\s*=\s*(.+)$', line)
        if m:
            out.append(m.group(1).strip())
        else:
            out.append(line)
    return out


# -------- expression parser / evaluator --------
TOKEN_RE = re.compile(r"""\s*(
    sqrt|pow|
    \d+(?:\.\d+)?|
    [()+\-*/,!^]
)""", re.X)


def tokenize(s: str):
    pos = 0
    tokens = []
    while pos < len(s):
        m = TOKEN_RE.match(s, pos)
        if not m:
            raise ValueError(f'Unexpected character at position {pos}: {s[pos:pos+20]!r}')
        tok = m.group(1)
        tokens.append(tok)
        pos = m.end()
    return tokens


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.i = 0

    def peek(self):
        return self.tokens[self.i] if self.i < len(self.tokens) else None

    def take(self, expected=None):
        tok = self.peek()
        if tok is None:
            raise ValueError('Unexpected end of expression')
        if expected is not None and tok != expected:
            raise ValueError(f'Expected {expected!r}, got {tok!r}')
        self.i += 1
        return tok

    def parse(self):
        node = self.parse_addsub()
        if self.peek() is not None:
            raise ValueError(f'Unexpected trailing token {self.peek()!r}')
        return node

    def parse_addsub(self):
        node = self.parse_muldiv()
        while self.peek() in ('+', '-'):
            op = self.take()
            rhs = self.parse_muldiv()
            node = (op, node, rhs)
        return node

    def parse_muldiv(self):
        node = self.parse_power()
        while self.peek() in ('*', '/'):
            op = self.take()
            rhs = self.parse_power()
            node = (op, node, rhs)
        return node

    def parse_power(self):
        node = self.parse_unary()
        if self.peek() == '^':
            self.take('^')
            rhs = self.parse_power()  # right-associative
            node = ('pow', node, rhs)
        return node

    def parse_unary(self):
        if self.peek() == '-':
            self.take('-')
            return ('neg', self.parse_unary())
        node = self.parse_primary()
        while self.peek() == '!':
            self.take('!')
            node = ('!', node)
        return node

    def parse_primary(self):
        tok = self.peek()
        if tok is None:
            raise ValueError('Unexpected end of expression')
        if tok == '(':
            self.take('(')
            node = self.parse_addsub()
            self.take(')')
            return node
        if tok == 'sqrt':
            self.take('sqrt')
            self.take('(')
            node = self.parse_addsub()
            self.take(')')
            return ('sqrt', node)
        if tok == 'pow':
            self.take('pow')
            self.take('(')
            a = self.parse_addsub()
            self.take(',')
            b = self.parse_addsub()
            self.take(')')
            return ('pow', a, b)
        if re.fullmatch(r'\d+(?:\.\d+)?', tok):
            self.take()
            return ('num', float(tok))
        raise ValueError(f'Unexpected token {tok!r}')


def eval_ast(node):
    kind = node[0]
    if kind == 'num':
        return node[1]
    if kind == 'neg':
        return -eval_ast(node[1])
    if kind == '+':
        return eval_ast(node[1]) + eval_ast(node[2])
    if kind == '-':
        return eval_ast(node[1]) - eval_ast(node[2])
    if kind == '*':
        return eval_ast(node[1]) * eval_ast(node[2])
    if kind == '/':
        denom = eval_ast(node[2])
        if abs(denom) < EPS:
            raise ValueError('division by zero')
        return eval_ast(node[1]) / denom
    if kind == 'sqrt':
        x = eval_ast(node[1])
        if x < -EPS:
            raise ValueError('sqrt of negative')
        return math.sqrt(max(0.0, x))
    if kind == 'pow':
        a = eval_ast(node[1])
        b = eval_ast(node[2])
        return math.pow(a, b)
    if kind == '!':
        x = eval_ast(node[1])
        if abs(x - round(x)) > EPS or x < 0 or x > 12:
            raise ValueError('bad factorial')
        return math.factorial(int(round(x)))
    raise ValueError(f'Unknown node kind {kind!r}')


def evaluate_formula(expr: str):
    tokens = tokenize(expr)
    ast = Parser(tokens).parse()
    return eval_ast(ast)


def formula_matches_digit(expr: str, digit: int, count: int) -> bool:
    num_tokens = re.findall(r'\d+', expr)
    # Every numeric literal must be made only of the chosen digit.
    if any(set(tok) != {str(digit)} for tok in num_tokens):
        return False
    # Total used copies must match count.
    return sum(len(tok) for tok in num_tokens) == count


def pick_valid_formula(text: str, digit: int, target: int, count: int):
    for cand in extract_formula_candidates(text):
        try:
            if not formula_matches_digit(cand, digit, count):
                continue
            value = evaluate_formula(cand)
            if abs(value - target) < 1e-7:
                return cand
        except Exception:
            continue
    return None


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
            self._send(200, HTML_FILE.read_bytes(), 'text/html; charset=utf-8')
            return
        if path == '/health':
            payload = {
                'ok': True,
                'executable': str(find_executable()),
                'html': str(HTML_FILE),
            }
            self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')
            return
        self._send(404, b'Not found')

    def do_POST(self):
        global _current_proc
        path = urlparse(self.path).path

        if path == '/cancel':
            with _proc_lock:
                if _current_proc and _current_proc.poll() is None:
                    _kill_proc(_current_proc)
                    _current_proc = None
            self._send(200, json.dumps({'cancelled': True}).encode(), 'application/json; charset=utf-8')
            return

        if path != '/solve':
            self._send(404, b'Not found')
            return

        try:
            length = int(self.headers.get('Content-Length', '0'))
            data = json.loads(self.rfile.read(length).decode('utf-8'))
            digit = int(data.get('digit'))
            target = int(data.get('target'))
            max_digits = int(data.get('maxDigits', 8))
        except Exception as e:
            self._send(400, json.dumps({'found': False, 'message': f'Bad request: {e}'}).encode(), 'application/json; charset=utf-8')
            return

        exe = find_executable()

        # Kill any previously running solver
        with _proc_lock:
            if _current_proc and _current_proc.poll() is None:
                _kill_proc(_current_proc)

        # Run solver in new process group so we can kill it cleanly
        cmd = [str(exe), str(digit), str(target), str(max_digits)]
        proc = subprocess.Popen(
            cmd, cwd=BASE_DIR,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            start_new_session=True
        )

        with _proc_lock:
            _current_proc = proc

        try:
            stdout_bytes, stderr_bytes = proc.communicate(timeout=300)
            stdout = stdout_bytes.decode('utf-8', errors='replace')
            stderr = stderr_bytes.decode('utf-8', errors='replace')
        except subprocess.TimeoutExpired:
            _kill_proc(proc)
            with _proc_lock:
                _current_proc = None
            payload = {
                'found': False,
                'digits': max_digits,
                'stats': [],
                'message': f'Solver timed out after 300 seconds.',
            }
            self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')
            return

        with _proc_lock:
            _current_proc = None

        # Process was killed by /cancel
        if proc.returncode and proc.returncode < 0:
            payload = {
                'found': False,
                'digits': max_digits,
                'stats': [],
                'message': 'Cancelled.',
            }
            self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')
            return

        # Parse stats from stderr: lines like "  S[3] = 421 values"
        stats = []
        for m in re.finditer(r'S\[(\d+)\]\s*=\s*(\d+)', stderr):
            stats.append([int(m.group(1)), int(m.group(2))])

        # Try to find and validate the formula from stdout
        digits_found = None
        formula = None

        solved_m = re.search(r'SOLVED\s+in\s+(\d+)\s+digit', stdout)
        if solved_m:
            digits_found = int(solved_m.group(1))

        if digits_found:
            formula = pick_valid_formula(stdout, digit, target, digits_found)

        if formula:
            payload = {
                'found': True,
                'digits': digits_found,
                'formula': formula,
                'latex': '',
                'stats': stats,
                'message': '',
            }
        else:
            payload = {
                'found': False,
                'digits': max_digits,
                'stats': stats,
                'message': 'No solution found within the search limit.',
            }

        self._send(200, json.dumps(payload).encode(), 'application/json; charset=utf-8')


def main():
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f'Serving on http://{HOST}:{PORT}')
    print(f'Using executable: {find_executable()}')
    server.serve_forever()


if __name__ == '__main__':
    main()