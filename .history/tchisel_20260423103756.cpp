// ╔══════════════════════════════════════════════════╗
// ║  TCHISEL — Tchisla Solver (v2)                    ║
// ║  Express any number using a single digit           ║
// ║  Compile: g++ -std=c++17 -O2 -o tchisel tchisel.cpp           ║
// ║  Usage:   ./tchisel <digit> <target> [max_digits]  ║
// ║                                                    ║
// ║  Tree-based: stores derivation records (~40 bytes)  ║
// ║  instead of expression strings (~200+ bytes).       ║
// ║  Reconstructs expressions on demand.                ║
// ╚══════════════════════════════════════════════════╝

#include <iostream>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <cstdint>
#include <string>
#include <cstdlib>
#include <algorithm>

using namespace std;

const long long MAX_VAL = 1000000000000LL;
const int MAX_SET_SIZE = 2000000;

// ─── Operation tags ─────────────────────────────────────
const int OP_CONCAT  = 0;
const int OP_ADD     = 1;
const int OP_SUB     = 2;
const int OP_MUL     = 3;
const int OP_DIV     = 4;
const int OP_POW     = 5;
const int OP_SQRT    = 6;  // unary: sqrt(arg_a in same set)
const int OP_SQRTMUL = 7;  // binary: sqrt(arg_a * arg_b from different sets)
const int OP_FACT    = 8;
const int OP_NEG     = 9;
// sqrt^k(a^b) = a^(b/2^k) — handles overflow by reducing exponent
const int OP_POWRT1  = 10; // sqrt(a^b)
const int OP_POWRT2  = 11; // sqrt(sqrt(a^b))
const int OP_POWRT3  = 12; // sqrt(sqrt(sqrt(a^b)))
const int OP_POWRT4  = 13;
const int OP_POWRT5  = 14;

// ─── Derivation record ─────────────────────────────────
struct Deriv {
    int op;
    int set_a, set_b;
    long long arg_a, arg_b;
    int neg_count;
    int depth;
    int sqrt_wraps;  // for OP_POW: number of sqrt() wrapping the power

    int score() const { return neg_count * 1000 + depth; }
};

unordered_map<long long, Deriv> sets[10];

long long factorial_table[21];

void precompute_factorials() {
    factorial_table[0] = 1;
    factorial_table[1] = 1;
    for (int i = 2; i <= 20; i++) {
        factorial_table[i] = factorial_table[i - 1] * i;
        if (factorial_table[i] > MAX_VAL)
            factorial_table[i] = MAX_VAL + 1;
    }
}

// ─── Reconstruct expression string from derivation tree ─

string reconstruct(long long val, int n) {
    auto it = sets[n].find(val);
    if (it == sets[n].end()) return "?";
    const Deriv& d = it->second;

    switch (d.op) {
    case OP_CONCAT:
        return to_string(val);
    case OP_ADD:
        return "(" + reconstruct(d.arg_a, d.set_a) + " + "
                   + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_SUB:
        return reconstruct(d.arg_a, d.set_a) + " - "
                   + reconstruct(d.arg_b, d.set_b);
    case OP_MUL:
        return "(" + reconstruct(d.arg_a, d.set_a) + " * "
                   + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_DIV:
        return "(" + reconstruct(d.arg_a, d.set_a) + " / "
                   + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_POW:
        return "(" + reconstruct(d.arg_a, d.set_a) + " ^ "
                   + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_SQRT:
        return "sqrt(" + reconstruct(d.arg_a, n) + ")";
    case OP_SQRTMUL:
        return "sqrt(" + reconstruct(d.arg_a, d.set_a) + " * "
                       + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_FACT:
        return "(" + reconstruct(d.arg_a, n) + ")!";
    case OP_NEG:
        return "-(" + reconstruct(d.arg_a, n) + ")";
    default:
        // Handle OP_POWRT1..5: sqrt^k(a^b)
        if (d.op >= OP_POWRT1 && d.op <= OP_POWRT5) {
            int k = d.op - OP_POWRT1 + 1;
            string inner = "(" + reconstruct(d.arg_a, d.set_a) + " ^ "
                               + reconstruct(d.arg_b, d.set_b) + ")";
            for (int i = 0; i < k; i++)
                inner = "sqrt(" + inner + ")";
            return inner;
        }
        return "?";
    }
}

// ─── Insert / replace ───────────────────────────────────

bool try_insert(int n, long long val, const Deriv& d) {
    if (val > MAX_VAL || val < -MAX_VAL) return false;

    auto it = sets[n].find(val);
    if (it != sets[n].end()) {
        if (d.score() < it->second.score()) {
            it->second = d;
            return true;
        }
        return false;
    }

    if ((int)sets[n].size() >= MAX_SET_SIZE) return false;
    sets[n][val] = d;
    return true;
}

// ─── Unary operations (BFS fixpoint) ────────────────────

void apply_unary(int n) {
    vector<long long> frontier;
    frontier.reserve(sets[n].size());
    for (auto& [val, _] : sets[n])
        frontier.push_back(val);

    for (int round = 0; round < 6 && !frontier.empty(); round++) {
        vector<long long> next;

        for (long long val : frontier) {
            auto it = sets[n].find(val);
            if (it == sets[n].end()) continue;
            int nc = it->second.neg_count;
            int dp = it->second.depth;

            // sqrt
            if (val > 0) {
                long long sq = llround(sqrt((double)val));
                if (sq * sq == val && sq > 0 && sq < MAX_VAL) {
                    Deriv d;
                    d.op = OP_SQRT; d.set_a = n; d.set_b = 0;
                    d.arg_a = val; d.arg_b = 0;
                    d.neg_count = nc; d.depth = dp + 1; d.sqrt_wraps = 0;
                    if (try_insert(n, sq, d))
                        next.push_back(sq);
                }
            }

            // factorial (3..12)
            if (val >= 3 && val <= 12) {
                long long f = factorial_table[val];
                if (f <= MAX_VAL) {
                    Deriv d;
                    d.op = OP_FACT; d.set_a = n; d.set_b = 0;
                    d.arg_a = val; d.arg_b = 0;
                    d.neg_count = nc; d.depth = dp + 1; d.sqrt_wraps = 0;
                    if (try_insert(n, f, d))
                        next.push_back(f);
                }
            }

            // negation (positive only)
            if (val > 0) {
                Deriv d;
                d.op = OP_NEG; d.set_a = n; d.set_b = 0;
                d.arg_a = val; d.arg_b = 0;
                d.neg_count = nc + 1; d.depth = dp + 1; d.sqrt_wraps = 0;
                if (try_insert(n, -val, d))
                    next.push_back(-val);
            }
        }

        frontier = move(next);
    }
}

// ─── Safe exponentiation ────────────────────────────────

long long safe_pow(long long base, long long exp) {
    if (exp < 0 || base <= 0) return 0;
    if (exp == 0) return 1;
    if (base == 1) return 1;
    if (base > 1000 || exp > 40) return 0;

    long long result = 1;
    for (long long i = 0; i < exp; i++) {
        result *= base;
        if (result > MAX_VAL) return 0;
    }
    return result;
}

// ─── Solver ─────────────────────────────────────────────

struct Result {
    bool found;
    int digits;
    string expression;
};

Result solve(int digit, long long target, int max_digits) {
    precompute_factorials();
    for (int i = 0; i <= 9; i++) sets[i].clear();

    for (int n = 1; n <= max_digits; n++) {
        // Concatenation
        long long concat = 0;
        for (int k = 0; k < n; k++) concat = concat * 10 + digit;
        Deriv cd;
        cd.op = OP_CONCAT; cd.set_a = 0; cd.set_b = 0;
        cd.arg_a = 0; cd.arg_b = 0;
        cd.neg_count = 0; cd.depth = 0; cd.sqrt_wraps = 0;
        try_insert(n, concat, cd);

        // Combine sets[i] x sets[j]
        for (int i = 1; i <= n / 2; i++) {
            int j = n - i;
            if (sets[i].empty() || sets[j].empty()) continue;

            for (auto& [v1, d1] : sets[i]) {
                if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                int nc1 = d1.neg_count, dp1 = d1.depth;

                for (auto& [v2, d2] : sets[j]) {
                    if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                    int nc = nc1 + d2.neg_count;
                    int dp = max(dp1, d2.depth) + 1;

                    auto make = [&](int op, int sa, int sb, long long a, long long b, int neg_extra) {
                        Deriv d;
                        d.op = op; d.set_a = sa; d.set_b = sb;
                        d.arg_a = a; d.arg_b = b;
                        d.neg_count = nc + neg_extra; d.depth = dp;
                        d.sqrt_wraps = 0;
                        return d;
                    };

                    // Addition
                    {
                        long long r = v1 + v2;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, make(OP_ADD, i, j, v1, v2, 0));
                    }

                    // Subtraction (both ways)
                    {
                        long long r = v1 - v2;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, make(OP_SUB, i, j, v1, v2, 0));
                    }
                    if (i != j) {
                        long long r = v2 - v1;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, make(OP_SUB, j, i, v2, v1, 0));
                    }

                    // Multiplication
                    if (v1 != 0 && v2 != 0 && abs(v1) <= MAX_VAL / max(abs(v2), 1LL)) {
                        long long r = v1 * v2;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, make(OP_MUL, i, j, v1, v2, 0));
                    }

                    // Division (both ways, exact only)
                    if (v2 != 0 && v1 % v2 == 0)
                        try_insert(n, v1 / v2, make(OP_DIV, i, j, v1, v2, 0));
                    if (i != j && v1 != 0 && v2 % v1 == 0)
                        try_insert(n, v2 / v1, make(OP_DIV, j, i, v2, v1, 0));

                    // Exponentiation (positive bases only)
                    // Also try sqrt-reduced powers: sqrt^k(b^e) = b^(e/2^k)
                    // This finds solutions like sqrt(sqrt(sqrt(12^24))) = 12^3
                    // without ever materializing the huge intermediate 12^24.
                    auto try_pow = [&](int sa, int sb, long long base, long long exp) {
                        if (base <= 0 || exp < 0) return;
                        // Direct power
                        if (exp <= 40) {
                            long long p = safe_pow(base, exp);
                            if (p > 0)
                                try_insert(n, p, make(OP_POW, sa, sb, base, exp, 0));
                        }
                        // Sqrt-reduced powers: try e/2, e/4, e/8, ...
                        long long reduced = exp;
                        for (int k = 1; k <= 5; k++) {
                            if (reduced % 2 != 0) break;
                            reduced /= 2;
                            if (reduced > 40 || reduced < 1) continue;
                            long long p = safe_pow(base, reduced);
                            if (p > 0) {
                                int op = OP_POWRT1 + (k - 1);
                                try_insert(n, p, make(op, sa, sb, base, exp, 0));
                            }
                        }
                    };

                    if (v1 > 0 && v2 >= 0 && v2 <= 10000)
                        try_pow(i, j, v1, v2);
                    if (i != j && v2 > 0 && v1 >= 0 && v1 <= 10000)
                        try_pow(j, i, v2, v1);

                    // sqrt of product
                    if (v1 > 0 && v2 > 0 && v1 <= MAX_VAL / max(v2, 1LL)) {
                        long long prod = v1 * v2;
                        long long sq = llround(sqrt((double)prod));
                        if (sq * sq == prod && sq <= MAX_VAL)
                            try_insert(n, sq, make(OP_SQRTMUL, i, j, v1, v2, 0));
                    }
                }
            }
        }

        apply_unary(n);

        cerr << "  S[" << n << "] = " << sets[n].size() << " values" << endl;

        if (sets[n].count(target)) {
            return {true, n, reconstruct(target, n)};
        }
    }

    return {false, max_digits, ""};
}

// ─── Main ───────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "╔════════════════════════════════════════╗" << endl;
        cerr << "║       TCHISEL — Tchisla Solver (v2)    ║" << endl;
        cerr << "╠════════════════════════════════════════╣" << endl;
        cerr << "║  Usage:                                ║" << endl;
        cerr << "║    ./tchisel <digit> <target> [max]    ║" << endl;
        cerr << "║                                        ║" << endl;
        cerr << "║  Examples:                             ║" << endl;
        cerr << "║    ./tchisel 2 100                     ║" << endl;
        cerr << "║    ./tchisel 8 1024                    ║" << endl;
        cerr << "║    ./tchisel 3 42 7                    ║" << endl;
        cerr << "╚════════════════════════════════════════╝" << endl;
        return 1;
    }

    int digit = atoi(argv[1]);
    long long target = atoll(argv[2]);
    int max_digits = (argc >= 4) ? atoi(argv[3]) : 8;

    if (digit < 1 || digit > 9) {
        cerr << "Error: digit must be 1-9" << endl;
        return 1;
    }
    if (target == 0) {
        cerr << "Error: target must be nonzero" << endl;
        return 1;
    }
    if (max_digits < 1 || max_digits > 9) {
        cerr << "Error: max digits must be 1-9" << endl;
        return 1;
    }

    cerr << endl;
    cerr << "  TCHISEL v2" << endl;
    cerr << "  Digit:  " << digit << endl;
    cerr << "  Target: " << target << endl;
    cerr << "  Max:    " << max_digits << " digits" << endl;
    cerr << endl;

    Result res = solve(digit, target, max_digits);

    if (res.found) {
        cout << endl;
        cout << "  SOLVED in " << res.digits << " digit(s)!" << endl;
        cout << "  " << target << " = " << res.expression << endl;
        cout << endl;
    } else {
        cout << endl;
        cout << "  No solution found within " << max_digits << " digits." << endl;
        cout << "  Try increasing the max." << endl;
        cout << endl;
    }

    return 0;
}