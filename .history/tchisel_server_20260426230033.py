#!/usr/bin/env python3
import json
import math
import os
import re
import signal
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

BASE_DIR = Path(__file__).resolve().parent
E

ORIGINAL_EXECUTABLE = BASE_DIR / 'tchiselO'
RATIONAL_EXECUTABLE = BASE_DIR / 'tchiselRO'
IRRATIONAL_EXECUTABLE = BASE_DIR / 'tchiselMR'

HTML_FILE = BASE_DIR / 'index.html'
HOST = '127.0.0.1'
PORT = 8080

EPS = 1e-9
PRIMARY_TIMEOUT = 300
BACKGROUND_TIMEOUT = 300

# Practical caps for background solvers.
# Raise these only if your machine can handle the memory/time.
ORIGINAL_MAX_DIGITS = 12
RATIONAL_MAX_DIGITS = 9
IRRATIONAL_MAX_DIGITS = 8

_proc_lock = threading.Lock()
_running_procs = {}

_jobs_lock = threading.Lock()
_jobs = {}
MAX_JOBS = 30

ANSI_RE = re.compile(r'\x1B\[[0-?]*[ -/]*[@-~]')
STATUS_LINE_RE = [
    re.compile(r'^found\s+\d+\s+solution', re.I),
    re.compile(r'^solved\s+in\s+\d+\s+digit\(s\)!?$', re.I),
    re.compile(r'^solution\s+found', re.I),
    re.compile(r'^usage:', re.I),
    re.compile(r'^no\s+solution\s+found', re.I),
]


# ─────────────────────────────────────────────────────────
# Process management
# ─────────────────────────────────────────────────────────

def _kill_proc(proc):
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


def _register_proc(proc):
    with _proc_lock:
        _running_procs[proc.pid] = proc


def _unregister_proc(proc):
    with _proc_lock:
        _running_procs.pop(proc.pid, None)


def _kill_all_running():
    with _proc_lock:
        procs = list(_running_procs.values())
        _running_procs.clear()

    for proc in procs:
        if proc.poll() is None:
            _kill_proc(proc)


def _mark_running_jobs_cancelled():
    with _jobs_lock:
        for job in _jobs.values():
            if job.get('status') == 'running':
                job['status'] = 'cancelled'
                job['message'] = 'Cancelled.'


# ─────────────────────────────────────────────────────────
# Executable lookup
# ─────────────────────────────────────────────────────────

def find_executable(kind='original') -> Path:
    if kind == 'rational':
        candidates = [
            RATIONAL_EXECUTABLE,
            BASE_DIR / 'tchiselR',
            BASE_DIR / 'tchisel_rational',
            BASE_DIR / 'tchiselR.exe',
            BASE_DIR / 'tchisel_rational.exe',
        ]
    elif kind == 'irrational':
        candidates = [
            IRRATIONAL_EXECUTABLE,
            BASE_DIR / 'tchiselI',
            BASE_DIR / 'tchisel_symbolic',
            BASE_DIR / 'tchisel_irrational',
            BASE_DIR / 'tchiselI.exe',
            BASE_DIR / 'tchisel_symbolic.exe',
            BASE_DIR / 'tchisel_irrational.exe',
        ]
    else:
        candidates = [
            ORIGINAL_EXECUTABLE,
            BASE_DIR / 'tchisel',
            BASE_DIR / 'solver',
            BASE_DIR / 'tchisla_solver',
            BASE_DIR / 'tchisel.exe',
        ]

    for c in candidates:
        if c.exists() and os.access(c, os.X_OK) and c.is_file():
            return c

    return candidates[0]


def solver_plan(max_digits: int):
    return [
        {
            'kind': 'original',
            'source': 'tchisel',
            'exe': find_executable('original'),
            'max_digits': min(max_digits, ORIGINAL_MAX_DIGITS),
            'timeout': PRIMARY_TIMEOUT,
        },
        {
            'kind': 'rational',
            'source': 'tchisel_rational',
            'exe': find_executable('rational'),
            'max_digits': min(max_digits, RATIONAL_MAX_DIGITS),
            'timeout': BACKGROUND_TIMEOUT,
        },
        {
            'kind': 'irrational',
            'source': 'tchisel_irrational',
            'exe': find_executable('irrational'),
            'max_digits': min(max_digits, IRRATIONAL_MAX_DIGITS),
            'timeout': BACKGROUND_TIMEOUT,
        },
    ]


# ─────────────────────────────────────────────────────────
# Solver output parsing
# ─────────────────────────────────────────────────────────

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

    return any(tok in line for tok in (
        'pow(', 'sqrt(', '+', '-', '*', '/', '^', '(', ')', '!', '='
    ))


def extract_formula_candidates(text: str):
    lines = clean_solver_lines(text)
    out = []

    for line in lines:
        if not looks_like_formula(line):
            continue

        # If line looks like "3141 = expr", keep only expr.
        m = re.match(r'^\s*-?\d+(?:\.\d+)?\s*=\s*(.+)$', line)
        if m:
            out.append(m.group(1).strip())
        else:
            out.append(line)

    return out


# ─────────────────────────────────────────────────────────
# Expression parser / evaluator
# ─────────────────────────────────────────────────────────

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
            rhs = self.parse_power()
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
    # Auto-close unbalanced parens
    opens = expr.count('(')
    closes = expr.count(')')
    if opens > closes:
        expr = expr + ')' * (opens - closes)

    tokens = tokenize(expr)
    ast = Parser(tokens).parse()
    return eval_ast(ast)


def formula_matches_digit(expr: str, digit: int, count: int) -> bool:
    num_tokens = re.findall(r'\d+', expr)

    # Every numeric literal must be made only of the chosen digit.
    if any(set(tok) != {str(digit)} for tok in num_tokens):
        return False

    # Total used copies must match the solver-reported digit count.
    return sum(len(tok) for tok in num_tokens) == count


def pick_valid_formula(text: str, digit: int, target: int, count: int):
    candidates = extract_formula_candidates(text)
    if not candidates:
        print(f'  [validate] no formula candidates found in solver output')
        return None

    for cand in candidates:
        try:
            if not formula_matches_digit(cand, digit, count):
                num_tokens = re.findall(r'\d+', cand)
                total = sum(len(tok) for tok in num_tokens)
                wrong_digits = [tok for tok in num_tokens if set(tok) != {str(digit)}]
                print(f'  [validate] digit check failed: need {count}×{digit}, got {total} digits, wrong tokens={wrong_digits}')
                continue

            value = evaluate_formula(cand)
            diff = abs(value - target)

            if diff < 0.5:  # wider tolerance for float accumulation through irrationals
                print(f'  [validate] ✓ formula valid: {cand[:60]}... eval={value} diff={diff:.2e}')
                return cand
            else:
                print(f'  [validate] eval mismatch: expected {target}, got {value} (diff={diff:.2e})')
        except Exception as e:
            print(f'  [validate] eval error: {e} | formula: {cand[:60]}...')
            continue

    return None


# ─────────────────────────────────────────────────────────
# Result comparison
# ─────────────────────────────────────────────────────────

def expression_score(result):
    """
    Lower is better.

    Primary:
        found beats not found

    Then:
        fewer digits

    Then:
        shorter formula
    """
    if not result or not result.get('found'):
        return (1, 10**9, 10**9)

    formula = result.get('formula') or ''
    return (0, int(result.get('digits', 10**9)), len(formula))


def is_better(candidate, incumbent) -> bool:
    return expression_score(candidate) < expression_score(incumbent)


# ─────────────────────────────────────────────────────────
# Solver subprocess runner
# ─────────────────────────────────────────────────────────

def parse_solver_output(stdout: str, stderr: str, digit: int, target: int, max_digits: int, source: str):
    stats = []

    for m in re.finditer(r'S\[(\d+)\]\s*=\s*(\d+)', stderr or ''):
        stats.append([int(m.group(1)), int(m.group(2))])

    digits_found = None
    formula = None

    solved_m = re.search(r'SOLVED\s+in\s+(\d+)\s+digit', stdout or '', re.I)
    if solved_m:
        digits_found = int(solved_m.group(1))
        formula = pick_valid_formula(stdout, digit, target, digits_found)

    if formula:
        return {
            'found': True,
            'digits': digits_found,
            'formula': formula,
            'latex': '',
            'stats': stats,
            'message': '',
            'source': source,
        }

    return {
        'found': False,
        'digits': max_digits,
        'formula': '',
        'latex': '',
        'stats': stats,
        'message': 'No solution found within the search limit.',
        'source': source,
    }


def run_solver(exe: Path, digit: int, target: int, max_digits: int, source: str, timeout: int):
    if not exe.exists():
        return {
            'found': False,
            'digits': max_digits,
            'formula': '',
            'latex': '',
            'stats': [],
            'message': f'{source} executable not found: {exe}',
            'source': source,
            'error': 'missing_executable',
        }

    if not os.access(exe, os.X_OK):
        return {
            'found': False,
            'digits': max_digits,
            'formula': '',
            'latex': '',
            'stats': [],
            'message': f'{source} is not executable: {exe}',
            'source': source,
            'error': 'not_executable',
        }

    cmd = [str(exe), str(digit), str(target), str(max_digits)]

    try:
        proc = subprocess.Popen(
            cmd,
            cwd=BASE_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )

        _register_proc(proc)

        try:
            stdout_bytes, stderr_bytes = proc.communicate(timeout=timeout)

            stdout = stdout_bytes.decode('utf-8', errors='replace')
            stderr = stderr_bytes.decode('utf-8', errors='replace')

        except subprocess.TimeoutExpired:
            _kill_proc(proc)
            return {
                'found': False,
                'digits': max_digits,
                'formula': '',
                'latex': '',
                'stats': [],
                'message': f'{source} timed out after {timeout} seconds.',
                'source': source,
                'error': 'timeout',
            }

        finally:
            _unregister_proc(proc)

        if proc.returncode and proc.returncode < 0:
            return {
                'found': False,
                'digits': max_digits,
                'formula': '',
                'latex': '',
                'stats': [],
                'message': f'{source} cancelled.',
                'source': source,
                'error': 'cancelled',
            }

        result = parse_solver_output(stdout, stderr, digit, target, max_digits, source)

        if proc.returncode not in (0, None) and not result.get('found'):
            result['message'] = f'{source} exited with code {proc.returncode}.'
            result['stdout'] = stdout[-2000:]
            result['stderr'] = stderr[-2000:]

        return result

    except Exception as e:
        return {
            'found': False,
            'digits': max_digits,
            'formula': '',
            'latex': '',
            'stats': [],
            'message': f'{source} failed: {e}',
            'source': source,
            'error': 'exception',
        }


# ─────────────────────────────────────────────────────────
# Parallel job logic
# ─────────────────────────────────────────────────────────

def _prune_jobs_locked():
    if len(_jobs) <= MAX_JOBS:
        return

    old = sorted(_jobs.items(), key=lambda kv: kv[1].get('created_at', 0))

    for job_id, _ in old[: max(0, len(old) - MAX_JOBS)]:
        _jobs.pop(job_id, None)


def publish_solver_result(job_id: str, result: dict):
    source = result.get('source', '?')
    with _jobs_lock:
        job = _jobs.get(job_id)

        if not job or job.get('status') == 'cancelled':
            print(f'[{source}] job cancelled, discarding result')
            return

        job.setdefault('candidates', []).append(result)
        job['candidate'] = result

        current_best = job.get('best')

        if is_better(result, current_best):
            old_digits = current_best.get('digits', '?') if current_best and current_best.get('found') else 'none'
            new_digits = result.get('digits', '?')
            print(f'[{source}] ★ new best! {old_digits} → {new_digits} digits')
            job['best'] = result
            job['improved'] = True
            job['message'] = 'a better solution is found'
            job['improvementVersion'] = int(job.get('improvementVersion', 0)) + 1
        else:
            best = current_best
            best_d = best.get('digits', '?') if best and best.get('found') else 'none'
            print(f'[{source}] not better (current best: {best_d} digits)')

        job['finishedCount'] = int(job.get('finishedCount', 0)) + 1

        if job['finishedCount'] >= job.get('totalCount', 3):
            job['status'] = 'done'
            job['stage'] = ''
            job['finished_at'] = time.time()
            print(f'[job] all solvers finished. best: {job["best"].get("digits") if job.get("best") and job["best"].get("found") else "none"} digits from {job["best"].get("source") if job.get("best") else "?"}')


def run_solver_for_job(job_id: str, solver: dict, digit: int, target: int, requested_max_digits: int):
    source = solver['source']
    print(f'[{source}] starting: digit={digit} target={target} max={solver["max_digits"]} exe={solver["exe"]}')

    with _jobs_lock:
        job = _jobs.get(job_id)
        if job and job.get('status') != 'cancelled':
            job['stage'] = source

    result = run_solver(
        solver['exe'],
        digit,
        target,
        solver['max_digits'],
        source,
        solver['timeout'],
    )

    result['requestedMaxDigits'] = requested_max_digits
    result['actualMaxDigits'] = solver['max_digits']

    found = result.get('found', False)
    digits = result.get('digits', '?')
    formula = result.get('formula', '')[:80]
    error = result.get('error', '')
    msg = result.get('message', '')[:100]

    if found:
        print(f'[{source}] ✓ SOLVED in {digits} digit(s): {formula}')
    elif error:
        print(f'[{source}] ✗ error={error}: {msg}')
    else:
        print(f'[{source}] ✗ no solution found: {msg}')

    publish_solver_result(job_id, result)


# ─────────────────────────────────────────────────────────
# HTTP handler
# ─────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # Keep terminal output focused on solver logs.
        pass

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

    def _send_json(self, status: int, payload: dict):
        self._send(
            status,
            json.dumps(payload).encode(),
            'application/json; charset=utf-8'
        )

    def do_OPTIONS(self):
        self._send(204, b'')

    def do_GET(self):
        path = urlparse(self.path).path

        if path in ('/', '/index.html'):
            self._send(200, HTML_FILE.read_bytes(), 'text/html; charset=utf-8')
            return

        if path == '/health':
            original = find_executable('original')
            rational = find_executable('rational')
            irrational = find_executable('irrational')

            self._send_json(200, {
                'ok': True,

                'originalExecutable': str(original),
                'originalExists': original.exists() and os.access(original, os.X_OK),
                'originalMaxDigits': ORIGINAL_MAX_DIGITS,

                'rationalExecutable': str(rational),
                'rationalExists': rational.exists() and os.access(rational, os.X_OK),
                'rationalMaxDigits': RATIONAL_MAX_DIGITS,

                'irrationalExecutable': str(irrational),
                'irrationalExists': irrational.exists() and os.access(irrational, os.X_OK),
                'irrationalMaxDigits': IRRATIONAL_MAX_DIGITS,

                'html': str(HTML_FILE),
            })
            return

        if path == '/improve':
            qs = parse_qs(urlparse(self.path).query)
            job_id = (qs.get('jobId') or [''])[0]

            with _jobs_lock:
                job = dict(_jobs.get(job_id) or {})

            if not job:
                self._send_json(404, {
                    'found': False,
                    'message': 'Unknown improvement job.',
                })
                return

            self._send_json(200, {
                'jobId': job_id,
                'running': job.get('status') == 'running',
                'status': job.get('status'),
                'stage': job.get('stage', ''),
                'improved': bool(job.get('improved')),
                'version': int(job.get('improvementVersion', 0)),
                'best': job.get('best'),
                'candidate': job.get('candidate'),
                'candidates': job.get('candidates', []),
                'finishedCount': int(job.get('finishedCount', 0)),
                'totalCount': int(job.get('totalCount', 3)),
                'message': job.get('message', ''),
            })
            return

        self._send(404, b'Not found')

    def do_POST(self):
        path = urlparse(self.path).path

        if path == '/cancel':
            _kill_all_running()
            _mark_running_jobs_cancelled()
            self._send_json(200, {'cancelled': True})
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

            if digit < 1 or digit > 9:
                raise ValueError('digit must be 1-9')

            if target == 0:
                raise ValueError('target must be nonzero')

            if max_digits < 1:
                max_digits = 1

            if max_digits > 12:
                max_digits = 12

        except Exception as e:
            self._send_json(400, {
                'found': False,
                'message': f'Bad request: {e}',
            })
            return

        # New solve supersedes older solve/background search.
        _kill_all_running()
        _mark_running_jobs_cancelled()

        job_id = str(uuid.uuid4())
        plan = solver_plan(max_digits)

        with _jobs_lock:
            _jobs[job_id] = {
                'created_at': time.time(),
                'status': 'running',
                'stage': '',

                'improved': False,
                'improvementVersion': 0,

                'best': None,
                'candidate': None,
                'candidates': [],

                'finishedCount': 0,
                'totalCount': len(plan),

                'message': '',
            }
            _prune_jobs_locked()

        # Start all solvers at the same time.
        for solver in plan:
            threading.Thread(
                target=run_solver_for_job,
                args=(job_id, solver, digit, target, max_digits),
                daemon=True,
            ).start()

        # Wait until any solver finds a valid solution, or until all solvers finish.
        first_result = None
        deadline = time.time() + PRIMARY_TIMEOUT

        while time.time() < deadline:
            with _jobs_lock:
                job = _jobs.get(job_id)

                if not job:
                    break

                first_result = job.get('best')

                finished_count = int(job.get('finishedCount', 0))
                total_count = int(job.get('totalCount', len(plan)))
                all_done = finished_count >= total_count

                version = int(job.get('improvementVersion', 0))
                status = job.get('status')

            if first_result:
                break

            if all_done or status in ('done', 'cancelled'):
                break

            time.sleep(0.05)

        with _jobs_lock:
            job = _jobs.get(job_id) or {}
            best = job.get('best')
            candidate = job.get('candidate')
            version = int(job.get('improvementVersion', 0))
            running = job.get('status') == 'running'

        if best:
            payload = dict(best)
        elif candidate:
            payload = dict(candidate)
        else:
            payload = {
                'found': False,
                'digits': max_digits,
                'formula': '',
                'latex': '',
                'stats': [],
                'message': 'No solver returned within the timeout.',
                'source': 'parallel',
            }

        payload['jobId'] = job_id
        payload['backgroundRunning'] = running
        payload['backgroundMessage'] = ''
        payload['version'] = version

        # Log what we're returning
        found = payload.get('found')
        digits = payload.get('digits')
        source = payload.get('source', '?')
        print(f'[response] returning: found={found} digits={digits} from={source} background_running={running}')

        self._send_json(200, payload)


# ─────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────

def main():
    server = ThreadingHTTPServer((HOST, PORT), Handler)

    print(f'Serving on http://{HOST}:{PORT}')
    print(f'Original executable:   {find_executable("original")}')
    print(f'Rational executable:   {find_executable("rational")}')
    print(f'Irrational executable: {find_executable("irrational")}')
    print()
    print(f'Original max digits:   {ORIGINAL_MAX_DIGITS}')
    print(f'Rational max digits:   {RATIONAL_MAX_DIGITS}')
    print(f'Irrational max digits: {IRRATIONAL_MAX_DIGITS}')

    server.serve_forever()


if __name__ == '__main__':
    main()