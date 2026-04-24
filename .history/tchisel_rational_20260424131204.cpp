// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  TCHISEL — Tchisla Solver with rationals   ║
// ║  Compile: g++ -std=c++17 -O2 -o tchisel_rational tchisel_rational.cpp ║
// ║  Usage:   ./tchisel_rational <digit> <target> [max_digits]             ║
// ╚════════════════════════════════════════════╝

#include <iostream>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <cstdint>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <numeric>

using namespace std;

const long long MAX_VAL = 1000000000000LL;
const long long MAX_DEN = 1000000LL;
const int MAX_SET_SIZE = 2000000;

// ─── Operation tags ─────────────────────────────────────
const int OP_CONCAT  = 0;
const int OP_ADD     = 1;
const int OP_SUB     = 2;
const int OP_MUL     = 3;
const int OP_DIV     = 4;
const int OP_POW     = 5;
const int OP_SQRT    = 6;
const int OP_SQRTMUL = 7;
const int OP_FACT    = 8;
const int OP_NEG     = 9;
const int OP_POWRT1  = 10;
const int OP_POWRT2  = 11;
const int OP_POWRT3  = 12;
const int OP_POWRT4  = 13;
const int OP_POWRT5  = 14;

static __int128 abs128(__int128 x) { return x < 0 ? -x : x; }

static __int128 gcd128(__int128 a, __int128 b) {
    a = abs128(a);
    b = abs128(b);
    while (b != 0) {
        __int128 r = a % b;
        a = b;
        b = r;
    }
    return a == 0 ? 1 : a;
}

struct Rat {
    long long num = 0;
    long long den = 1;

    bool operator==(const Rat& other) const {
        return num == other.num && den == other.den;
    }
};

struct RatHash {
    size_t operator()(const Rat& r) const {
        uint64_t a = (uint64_t)r.num;
        uint64_t b = (uint64_t)r.den;
        a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
        return (size_t)a;
    }
};

bool make_rat(__int128 num, __int128 den, Rat& out) {
    if (den == 0) return false;
    if (num == 0) {
        out = {0, 1};
        return true;
    }
    if (den < 0) {
        num = -num;
        den = -den;
    }

    __int128 g = gcd128(num, den);
    num /= g;
    den /= g;

    if (abs128(num) > MAX_VAL || den > MAX_DEN) return false;

    out.num = (long long)num;
    out.den = (long long)den;
    return true;
}

Rat make_int(long long x) {
    return {x, 1};
}

bool is_integer(const Rat& x) {
    return x.den == 1;
}

string rat_to_string(const Rat& x) {
    if (x.den == 1) return to_string(x.num);
    return to_string(x.num) + "/" + to_string(x.den);
}

// ─── Derivation record ─────────────────────────────────
struct Deriv {
    int op;
    int set_a, set_b;
    Rat arg_a, arg_b;
    int neg_count;
    int depth;
    int sqrt_wraps;

    int score() const { return neg_count * 1000 + depth; }
};

unordered_map<Rat, Deriv, RatHash> sets[10];
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

// ─── Rational arithmetic ───────────────────────────────
bool add_rat(const Rat& a, const Rat& b, Rat& out) {
    return make_rat((__int128)a.num * b.den + (__int128)b.num * a.den,
                    (__int128)a.den * b.den, out);
}

bool sub_rat(const Rat& a, const Rat& b, Rat& out) {
    return make_rat((__int128)a.num * b.den - (__int128)b.num * a.den,
                    (__int128)a.den * b.den, out);
}

bool mul_rat(const Rat& a, const Rat& b, Rat& out) {
    return make_rat((__int128)a.num * b.num, (__int128)a.den * b.den, out);
}

bool div_rat(const Rat& a, const Rat& b, Rat& out) {
    if (b.num == 0) return false;
    return make_rat((__int128)a.num * b.den, (__int128)a.den * b.num, out);
}

bool neg_rat(const Rat& a, Rat& out) {
    return make_rat(-(__int128)a.num, a.den, out);
}

bool perfect_square_ll(long long x, long long& root) {
    if (x < 0) return false;
    long long r = llround(sqrt((long double)x));
    while ((__int128)r * r < x) r++;
    while ((__int128)r * r > x) r--;
    if ((__int128)r * r == x) {
        root = r;
        return true;
    }
    return false;
}

bool sqrt_rat(const Rat& a, Rat& out) {
    if (a.num <= 0) return false;
    long long rn, rd;
    if (!perfect_square_ll(a.num, rn)) return false;
    if (!perfect_square_ll(a.den, rd)) return false;
    return make_rat(rn, rd, out);
}

bool pow_ll_limited(long long base, long long exp, long long limit, long long& out) {
    if (exp < 0) return false;
    __int128 result = 1;
    for (long long i = 0; i < exp; i++) {
        result *= base;
        if (result > limit) return false;
    }
    out = (long long)result;
    return true;
}

bool pow_rat(const Rat& base, long long exp, Rat& out) {
    if (base.num <= 0) return false; // keep the same positive-base rule as your original solver
    if (exp == 0) return make_rat(1, 1, out);

    long long e = llabs(exp);
    if (e > 40) return false;

    long long pn, pd;
    if (!pow_ll_limited(base.num, e, MAX_VAL, pn)) return false;
    if (!pow_ll_limited(base.den, e, MAX_DEN, pd)) return false;

    if (exp > 0) return make_rat(pn, pd, out);
    return make_rat(pd, pn, out); // negative exponent: base^(-e) = 1/base^e
}

// ─── Reconstruct expression string from derivation tree ─
string reconstruct(const Rat& val, int n) {
    auto it = sets[n].find(val);
    if (it == sets[n].end()) return "?";
    const Deriv& d = it->second;

    switch (d.op) {
    case OP_CONCAT:
        return rat_to_string(val);
    case OP_ADD:
        return "(" + reconstruct(d.arg_a, d.set_a) + " + "
                   + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_SUB:
        return "(" + reconstruct(d.arg_a, d.set_a) + " - "
                   + reconstruct(d.arg_b, d.set_b) + ")";
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
bool try_insert(int n, const Rat& val, const Deriv& d) {
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
    vector<Rat> frontier;
    frontier.reserve(sets[n].size());
    for (auto& [val, _] : sets[n]) frontier.push_back(val);

    for (int round = 0; round < 6 && !frontier.empty(); round++) {
        vector<Rat> next;

        for (const Rat& val : frontier) {
            auto it = sets[n].find(val);
            if (it == sets[n].end()) continue;
            int nc = it->second.neg_count;
            int dp = it->second.depth;

            // sqrt, only when the rational has a rational square root
            Rat sq;
            if (sqrt_rat(val, sq)) {
                Deriv d;
                d.op = OP_SQRT; d.set_a = n; d.set_b = 0;
                d.arg_a = val; d.arg_b = make_int(0);
                d.neg_count = nc; d.depth = dp + 1; d.sqrt_wraps = 0;
                if (try_insert(n, sq, d)) next.push_back(sq);
            }

            // factorial, only on small integers
            if (is_integer(val) && val.num >= 3 && val.num <= 12) {
                long long f = factorial_table[val.num];
                if (f <= MAX_VAL) {
                    Deriv d;
                    d.op = OP_FACT; d.set_a = n; d.set_b = 0;
                    d.arg_a = val; d.arg_b = make_int(0);
                    d.neg_count = nc; d.depth = dp + 1; d.sqrt_wraps = 0;
                    Rat rf = make_int(f);
                    if (try_insert(n, rf, d)) next.push_back(rf);
                }
            }

            // negation
            if (val.num > 0) {
                Rat nv;
                if (neg_rat(val, nv)) {
                    Deriv d;
                    d.op = OP_NEG; d.set_a = n; d.set_b = 0;
                    d.arg_a = val; d.arg_b = make_int(0);
                    d.neg_count = nc + 1; d.depth = dp + 1; d.sqrt_wraps = 0;
                    if (try_insert(n, nv, d)) next.push_back(nv);
                }
            }
        }

        frontier = move(next);
    }
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

    Rat target_rat = make_int(target);

    for (int n = 1; n <= max_digits; n++) {
        // Concatenation
        long long concat = 0;
        for (int k = 0; k < n; k++) concat = concat * 10 + digit;
        Deriv cd;
        cd.op = OP_CONCAT; cd.set_a = 0; cd.set_b = 0;
        cd.arg_a = make_int(0); cd.arg_b = make_int(0);
        cd.neg_count = 0; cd.depth = 0; cd.sqrt_wraps = 0;
        try_insert(n, make_int(concat), cd);

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

                    auto make = [&](int op, int sa, int sb, const Rat& a, const Rat& b, int neg_extra) {
                        Deriv d;
                        d.op = op; d.set_a = sa; d.set_b = sb;
                        d.arg_a = a; d.arg_b = b;
                        d.neg_count = nc + neg_extra; d.depth = dp;
                        d.sqrt_wraps = 0;
                        return d;
                    };

                    Rat r;

                    if (add_rat(v1, v2, r))
                        try_insert(n, r, make(OP_ADD, i, j, v1, v2, 0));

                    if (sub_rat(v1, v2, r))
                        try_insert(n, r, make(OP_SUB, i, j, v1, v2, 0));
                    if (i != j && sub_rat(v2, v1, r))
                        try_insert(n, r, make(OP_SUB, j, i, v2, v1, 0));

                    if (mul_rat(v1, v2, r))
                        try_insert(n, r, make(OP_MUL, i, j, v1, v2, 0));

                    if (div_rat(v1, v2, r))
                        try_insert(n, r, make(OP_DIV, i, j, v1, v2, 0));
                    if (i != j && div_rat(v2, v1, r))
                        try_insert(n, r, make(OP_DIV, j, i, v2, v1, 0));

                    // Exponentiation: base can be rational, exponent must be an integer.
                    // This is the key fix for expressions like 2^(-2) = 1/4.
                    auto try_pow = [&](int sa, int sb, const Rat& base, const Rat& exp) {
                        if (!is_integer(exp)) return;
                        long long e = exp.num;
                        Rat p;
                        if (pow_rat(base, e, p))
                            try_insert(n, p, make(OP_POW, sa, sb, base, exp, 0));

                        // sqrt-reduced powers for large positive even exponents
                        long long reduced = e;
                        for (int k = 1; k <= 5; k++) {
                            if (reduced <= 0 || reduced % 2 != 0) break;
                            reduced /= 2;
                            if (pow_rat(base, reduced, p)) {
                                int op = OP_POWRT1 + (k - 1);
                                try_insert(n, p, make(op, sa, sb, base, exp, 0));
                            }
                        }
                    };

                    try_pow(i, j, v1, v2);
                    if (i != j) try_pow(j, i, v2, v1);

                    // sqrt of product, only if the product has a rational square root
                    Rat prod, sq;
                    if (mul_rat(v1, v2, prod) && sqrt_rat(prod, sq))
                        try_insert(n, sq, make(OP_SQRTMUL, i, j, v1, v2, 0));
                }
            }
        }

        apply_unary(n);

        cerr << "  S[" << n << "] = " << sets[n].size() << " values" << endl;

        if (sets[n].count(target_rat)) {
            return {true, n, reconstruct(target_rat, n)};
        }
    }

    return {false, max_digits, ""};
}

// ─── Main ───────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: ./tchisel_rational <digit> <target> [max]" << endl;
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
    cerr << "  TCHISEL rational" << endl;
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
        cout << endl;
    }

    return 0;
}
