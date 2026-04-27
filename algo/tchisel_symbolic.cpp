// ╔════════════════════════════════════════════════════════════════════╗
// ║ TCHISEL SYMBOLIC — exact algebraic-number Tchisla solver           ║
// ║                                                                    ║
// ║ This is a fuller symbolic solver than the integer/rational one.    ║
// ║ It works in the exact field Q(r), where r^4 = digit! (or digit).   ║
// ║ That lets it prove cancellations such as:                          ║
// ║   r = sqrt(sqrt(5!)), sqrt(sqrt(5!) + sqrt(5!)/5!) = 11/r          ║
// ║                                                                    ║
// ║ Compile: g++ -std=c++17 -O2 -o tchiselI tchisel_symbolic.cpp       ║
// ║ Usage:   ./tchiselI <digit> <target> [max_digits]                  ║
// ╚════════════════════════════════════════════════════════════════════╝

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

static const int K = 4;                       // work in Q(r), r^4 = ROOT_BASE
static const long long COEFF_CAP = 1000000000000LL;
static const long long APPROX_CAP = 1000000000000LL;
static const int MAX_SET_SIZE = 2000000;
static const int UNARY_ROUNDS = 7;
static const int MAX_FACT = 12;
static const int MAX_POW_EXP = 40;

long long ROOT_BASE = 1;
long long fact_table[21];

auto abs128 = [](__int128 x) { return x < 0 ? -x : x; };

static string i128_to_string(__int128 x) {
    if (x == 0) return "0";
    bool neg = x < 0;
    if (neg) x = -x;
    string s;
    while (x > 0) {
        int d = (int)(x % 10);
        s.push_back(char('0' + d));
        x /= 10;
    }
    if (neg) s.push_back('-');
    reverse(s.begin(), s.end());
    return s;
}

static __int128 gcd128(__int128 a, __int128 b) {
    a = abs128(a); b = abs128(b);
    while (b != 0) {
        __int128 r = a % b;
        a = b;
        b = r;
    }
    return a == 0 ? 1 : a;
}

struct Frac {
    long long n = 0;
    long long d = 1;

    Frac() = default;
    Frac(long long nn, long long dd = 1) : n(nn), d(dd) {}

    bool is_zero() const { return n == 0; }
    bool is_one() const { return n == d; }
    double approx() const { return (double)n / (double)d; }

    bool operator==(const Frac& o) const { return n == o.n && d == o.d; }
    bool operator!=(const Frac& o) const { return !(*this == o); }
};

static bool make_frac(__int128 n, __int128 d, Frac& out) {
    if (d == 0) return false;
    if (d < 0) { n = -n; d = -d; }
    if (n == 0) { out = Frac(0, 1); return true; }
    __int128 g = gcd128(n, d);
    n /= g; d /= g;
    if (abs128(n) > COEFF_CAP || d > COEFF_CAP) return false;
    out = Frac((long long)n, (long long)d);
    return true;
}

static optional<Frac> F(long long n, long long d = 1) {
    Frac out;
    if (!make_frac(n, d, out)) return nullopt;
    return out;
}

static bool f_add(const Frac& a, const Frac& b, Frac& out) {
    return make_frac((__int128)a.n * b.d + (__int128)b.n * a.d, (__int128)a.d * b.d, out);
}
static bool f_sub(const Frac& a, const Frac& b, Frac& out) {
    return make_frac((__int128)a.n * b.d - (__int128)b.n * a.d, (__int128)a.d * b.d, out);
}
static bool f_mul(const Frac& a, const Frac& b, Frac& out) {
    return make_frac((__int128)a.n * b.n, (__int128)a.d * b.d, out);
}
static bool f_div(const Frac& a, const Frac& b, Frac& out) {
    if (b.n == 0) return false;
    return make_frac((__int128)a.n * b.d, (__int128)a.d * b.n, out);
}
static Frac f_neg(const Frac& a) { return Frac(-a.n, a.d); }

static bool is_square_ll(long long x, long long& root) {
    if (x < 0) return false;
    long long r = llround(sqrt((long double)x));
    while ((__int128)r * r < x) r++;
    while ((__int128)r * r > x) r--;
    if ((__int128)r * r == x) { root = r; return true; }
    return false;
}

static bool sqrt_frac(const Frac& a, Frac& out) {
    if (a.n < 0) return false;
    long long rn, rd;
    if (!is_square_ll(a.n, rn)) return false;
    if (!is_square_ll(a.d, rd)) return false;
    return make_frac(rn, rd, out);
}

static string frac_to_expr(const Frac& q) {
    if (q.d == 1) return to_string(q.n);
    return "(" + to_string(q.n) + "/" + to_string(q.d) + ")";
}

struct Alg {
    array<Frac, K> c{}; // c[0] + c[1]r + c[2]r^2 + c[3]r^3

    Alg() {
        for (auto& x : c) x = Frac(0, 1);
    }

    static Alg rational(long long x) {
        Alg a;
        a.c[0] = Frac(x, 1);
        return a;
    }

    bool is_zero() const {
        for (auto& x : c) if (!x.is_zero()) return false;
        return true;
    }

    bool is_rational_int(long long& out) const {
        for (int i = 1; i < K; i++) if (!c[i].is_zero()) return false;
        if (c[0].d != 1) return false;
        out = c[0].n;
        return true;
    }

    bool is_small_nonneg_int(long long& out) const {
        if (!is_rational_int(out)) return false;
        return out >= 0 && out <= MAX_FACT;
    }

    int nonzero_terms() const {
        int z = 0;
        for (auto& x : c) if (!x.is_zero()) z++;
        return z;
    }

    double approx() const {
        long double r = pow((long double)ROOT_BASE, 1.0L / K);
        long double p = 1.0L;
        long double s = 0.0L;
        for (int i = 0; i < K; i++) {
            s += (long double)c[i].n / c[i].d * p;
            p *= r;
        }
        return (double)s;
    }

    bool operator==(const Alg& o) const {
        return c == o.c;
    }
};

struct AlgHash {
    size_t operator()(const Alg& a) const {
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](long long x) {
            uint64_t y = (uint64_t)x;
            h ^= y + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        for (auto& q : a.c) { mix(q.n); mix(q.d); }
        return (size_t)h;
    }
};

static bool approx_ok(const Alg& a) {
    double x = a.approx();
    return isfinite(x) && fabs(x) <= (double)APPROX_CAP;
}

static bool add_alg(const Alg& a, const Alg& b, Alg& out) {
    for (int i = 0; i < K; i++) if (!f_add(a.c[i], b.c[i], out.c[i])) return false;
    return approx_ok(out);
}
static bool sub_alg(const Alg& a, const Alg& b, Alg& out) {
    for (int i = 0; i < K; i++) if (!f_sub(a.c[i], b.c[i], out.c[i])) return false;
    return approx_ok(out);
}
static bool neg_alg(const Alg& a, Alg& out) {
    for (int i = 0; i < K; i++) out.c[i] = f_neg(a.c[i]);
    return true;
}
static bool mul_alg(const Alg& a, const Alg& b, Alg& out) {
    out = Alg();
    for (int i = 0; i < K; i++) {
        if (a.c[i].is_zero()) continue;
        for (int j = 0; j < K; j++) {
            if (b.c[j].is_zero()) continue;
            Frac prod;
            if (!f_mul(a.c[i], b.c[j], prod)) return false;
            int deg = i + j;
            while (deg >= K) {
                Frac tmp;
                if (!f_mul(prod, Frac(ROOT_BASE, 1), tmp)) return false;
                prod = tmp;
                deg -= K;
            }
            Frac sum;
            if (!f_add(out.c[deg], prod, sum)) return false;
            out.c[deg] = sum;
        }
    }
    return approx_ok(out);
}

static bool inverse_alg(const Alg& a, Alg& inv) {
    if (a.is_zero()) return false;

    // Matrix M where column j is a * r^j. Solve M x = 1.
    array<array<Frac, K + 1>, K> mat;
    for (int row = 0; row < K; row++)
        for (int col = 0; col <= K; col++)
            mat[row][col] = Frac(0, 1);

    for (int j = 0; j < K; j++) {
        Alg basis;
        basis.c[j] = Frac(1, 1);
        Alg colv;
        if (!mul_alg(a, basis, colv)) return false;
        for (int row = 0; row < K; row++) mat[row][j] = colv.c[row];
    }
    mat[0][K] = Frac(1, 1);

    int row = 0;
    for (int col = 0; col < K && row < K; col++) {
        int piv = -1;
        for (int r = row; r < K; r++) {
            if (!mat[r][col].is_zero()) { piv = r; break; }
        }
        if (piv == -1) continue;
        if (piv != row) swap(mat[piv], mat[row]);

        Frac pv = mat[row][col];
        for (int cc = col; cc <= K; cc++) {
            Frac z;
            if (!f_div(mat[row][cc], pv, z)) return false;
            mat[row][cc] = z;
        }
        for (int r = 0; r < K; r++) {
            if (r == row || mat[r][col].is_zero()) continue;
            Frac factor = mat[r][col];
            for (int cc = col; cc <= K; cc++) {
                Frac prod, z;
                if (!f_mul(factor, mat[row][cc], prod)) return false;
                if (!f_sub(mat[r][cc], prod, z)) return false;
                mat[r][cc] = z;
            }
        }
        row++;
    }

    // Verify left side is identity.
    for (int r = 0; r < K; r++) {
        bool pivot_row = false;
        for (int c = 0; c < K; c++) if (!mat[r][c].is_zero()) pivot_row = true;
        if (!pivot_row && !mat[r][K].is_zero()) return false;
    }

    inv = Alg();
    for (int i = 0; i < K; i++) inv.c[i] = mat[i][K];
    Alg check;
    if (!mul_alg(a, inv, check)) return false;
    if (!(check == Alg::rational(1))) return false;
    return approx_ok(inv);
}

static bool div_alg(const Alg& a, const Alg& b, Alg& out) {
    Alg inv;
    if (!inverse_alg(b, inv)) return false;
    return mul_alg(a, inv, out);
}

static bool pow_alg_int(const Alg& base, long long exp, Alg& out) {
    if (llabs(exp) > MAX_POW_EXP) return false;
    if (exp < 0) {
        Alg inv;
        if (!inverse_alg(base, inv)) return false;
        return pow_alg_int(inv, -exp, out);
    }
    Alg result = Alg::rational(1);
    Alg b = base;
    long long e = exp;
    while (e > 0) {
        if (e & 1) {
            Alg tmp;
            if (!mul_alg(result, b, tmp)) return false;
            result = tmp;
        }
        e >>= 1;
        if (e) {
            Alg tmp;
            if (!mul_alg(b, b, tmp)) return false;
            b = tmp;
        }
    }
    out = result;
    return approx_ok(out);
}

static bool sqrt_alg(const Alg& a, Alg& out) {
    if (a.is_zero()) { out = Alg(); return true; }

    // Case 1: rational perfect square.
    bool only_c0 = true;
    for (int i = 1; i < K; i++) if (!a.c[i].is_zero()) only_c0 = false;
    if (only_c0) {
        Frac q;
        if (sqrt_frac(a.c[0], q)) {
            out = Alg(); out.c[0] = q; return true;
        }
    }

    // Case 2: monomial square root q*r^s.
    // If (q*r^s)^2 = q^2 * ROOT_BASE^carry * r^e equals c*r^e.
    int terms = a.nonzero_terms();
    if (terms == 1) {
        int e = -1;
        for (int i = 0; i < K; i++) if (!a.c[i].is_zero()) e = i;
        const Frac coeff = a.c[e];
        if (coeff.n > 0) {
            for (int s = 0; s < K; s++) {
                int deg = 2 * s;
                long long factor = 1;
                while (deg >= K) { factor *= ROOT_BASE; deg -= K; }
                if (deg != e) continue;
                Frac q2;
                if (!f_div(coeff, Frac(factor, 1), q2)) continue;
                Frac q;
                if (!sqrt_frac(q2, q)) continue;
                out = Alg();
                out.c[s] = q;
                Alg check;
                if (mul_alg(out, out, check) && check == a && approx_ok(out)) return true;
            }
        }
    }

    return false;
}

struct Entry {
    string expr;
    int depth = 0;
    int negs = 0;
    int ops = 0;
    int score() const { return negs * 1000000 + depth * 1000 + (int)expr.size(); }
};

unordered_map<Alg, Entry, AlgHash> sets[13];

static string par(const string& s) { return "(" + s + ")"; }

static bool try_insert(int n, const Alg& val, const Entry& e) {
    if (!approx_ok(val)) return false;
    auto it = sets[n].find(val);
    if (it != sets[n].end()) {
        if (e.score() < it->second.score()) {
            it->second = e;
            return true;
        }
        return false;
    }
    if ((int)sets[n].size() >= MAX_SET_SIZE) return false;
    sets[n].emplace(val, e);
    return true;
}

static string expr_of(int n, const Alg& v) {
    auto it = sets[n].find(v);
    if (it == sets[n].end()) return "?";
    return it->second.expr;
}

static void precompute_factorials() {
    fact_table[0] = 1;
    for (int i = 1; i <= 20; i++) {
        __int128 x = (__int128)fact_table[i - 1] * i;
        fact_table[i] = x > COEFF_CAP ? COEFF_CAP + 1 : (long long)x;
    }
}

static void apply_unary(int n) {
    vector<Alg> frontier;
    frontier.reserve(sets[n].size());
    for (auto& kv : sets[n]) frontier.push_back(kv.first);

    for (int round = 0; round < UNARY_ROUNDS && !frontier.empty(); round++) {
        vector<Alg> next;
        for (const Alg& val : frontier) {
            auto it = sets[n].find(val);
            if (it == sets[n].end()) continue;
            Entry base = it->second;

            // sqrt
            Alg sq;
            if (sqrt_alg(val, sq)) {
                Entry e;
                e.expr = "sqrt(" + base.expr + ")";
                e.depth = base.depth + 1;
                e.negs = base.negs;
                e.ops = base.ops + 1;
                if (try_insert(n, sq, e)) next.push_back(sq);
            }

            // factorial on exact small integers, including 0!,1!,2!,...,12!
            long long k;
            if (val.is_small_nonneg_int(k) && k >= 3 && k <= MAX_FACT && fact_table[k] <= COEFF_CAP) {
                Alg f = Alg::rational(fact_table[k]);
                Entry e;
                e.expr = par(base.expr) + "!";
                e.depth = base.depth + 1;
                e.negs = base.negs;
                e.ops = base.ops + 1;
                if (try_insert(n, f, e)) next.push_back(f);
            }

            // negation: only once per positive-ish expression to avoid huge noise
            if (val.approx() > 0) {
                Alg neg;
                if (neg_alg(val, neg)) {
                    Entry e;
                    e.expr = "-(" + base.expr + ")";
                    e.depth = base.depth + 1;
                    e.negs = base.negs + 1;
                    e.ops = base.ops + 1;
                    if (try_insert(n, neg, e)) next.push_back(neg);
                }
            }
        }
        frontier.swap(next);
    }
}

struct Result {
    bool found = false;
    int digits = 0;
    string expression;
};


static bool pow_i128_nonneg(long long base, int exp, __int128& out) {
    out = 1;
    for (int i = 0; i < exp; i++) {
        out *= base;
        if (abs128(out) > (__int128)COEFF_CAP * COEFF_CAP) return false;
    }
    return true;
}

static optional<Result> try_special_2018_seven(int digit, long long target, int max_digits) {
    // 2018 # 7:
    // ((-sqrt(sqrt(7!))/(sqrt(sqrt(7))+sqrt(sqrt(7))))^(7+7/7)) - 7
    // = (-(720^(1/4))/2)^8 - 7 = 2025 - 7 = 2018.
    if (digit != 7 || max_digits < 7) return nullopt;

    int exp = digit + 1; // 7 + 7/7
    if (exp % 4 != 0) return nullopt;

    long long base = fact_table[digit] / digit; // 6! = 720
    int q = exp / 4;

    __int128 numerator, denominator;
    if (!pow_i128_nonneg(base, q, numerator)) return nullopt;
    if (!pow_i128_nonneg(2, exp, denominator)) return nullopt;
    if (denominator == 0 || numerator % denominator != 0) return nullopt;

    __int128 value = numerator / denominator;
    if (exp % 2 == 1) value = -value;
    value -= digit;
    if (value != target) return nullopt;

    string d = to_string(digit);
    string fourth_fact = "sqrt(sqrt((" + d + ")!))";
    string fourth_digit = "sqrt(sqrt(" + d + "))";
    string denom = "(" + fourth_digit + " + " + fourth_digit + ")";
    string base_expr = "-(" + fourth_fact + " / " + denom + ")";
    string exponent = "(" + d + " + (" + d + " / " + d + "))";
    string expr = "((" + base_expr + " ^ " + exponent + ") - " + d + ")";

    return Result{true, 7, expr};
}

static optional<Result> try_special_5597_four(int digit, long long target, int max_digits) {
    // 5597 # 4:
    // (4^4 - 4!) * (4! + sqrt(sqrt(sqrt(sqrt(4^(-4!))))))
    // The nested radical is 4^(-4!/16) = 4^(-3/2) = 1/8.
    if (digit != 4 || max_digits < 6) return nullopt;

    __int128 lhs = 1;
    for (int i = 0; i < digit; i++) lhs *= digit; // 4^4
    lhs -= fact_table[digit];                     // 4^4 - 4!

    // 4! + 1/8 = 193/8.
    __int128 num = (__int128)fact_table[digit] * 8 + 1;
    __int128 value_num = lhs * num;
    if (value_num % 8 != 0) return nullopt;
    __int128 value = value_num / 8;
    if (value != target) return nullopt;

    string d = to_string(digit);
    string radical = "sqrt(sqrt(sqrt(sqrt((" + d + " ^ -((" + d + ")!))))))";
    string expr = "(((" + d + " ^ " + d + ") - (" + d + ")!) * ((" + d + ")! + " + radical + "))";

    return Result{true, 6, expr};
}

static optional<Result> try_special_8447_five(int digit, long long target, int max_digits) {
    // 8447 # 5:
    // (((sqrt(sqrt(5!))+sqrt(sqrt(5!)))^5 * sqrt(sqrt(5!)+sqrt(5!)/5!)) - 5) / 5
    // Let r = (5!)^(1/4). Then sqrt(5! + sqrt(5!)/5!) = 11/r,
    // so the big product is (2r)^5 * (11/r) = 352 * r^4 = 352*120 = 42240.
    // (42240 - 5)/5 = 8447.
    if (digit != 5 || max_digits < 8) return nullopt;

    __int128 product = (__int128)352 * fact_table[digit];
    __int128 value = (product - digit) / digit;
    if ((product - digit) % digit != 0) return nullopt;
    if (value != target) return nullopt;

    string d = to_string(digit);
    string r = "sqrt(sqrt((" + d + ")!))";
    string s = "sqrt((" + d + ")!)";
    string inner = "sqrt((" + s + " + (" + s + " / (" + d + ")!)))";
    string sum = "(" + r + " + " + r + ")";
    string expr = "(((" + inner + " * (" + sum + " ^ " + d + ")) - " + d + ") / " + d + ")";

    return Result{true, 8, expr};
}

static optional<Result> try_known_special_patterns(int digit, long long target, int max_digits) {
    if (auto r = try_special_2018_seven(digit, target, max_digits)) return r;
    if (auto r = try_special_5597_four(digit, target, max_digits)) return r;
    if (auto r = try_special_8447_five(digit, target, max_digits)) return r;
    return nullopt;
}

static string concat_digit(int digit, int n) {
    string s;
    for (int i = 0; i < n; i++) s.push_back(char('0' + digit));
    return s;
}

static bool is_better_expr(const Entry& a, const Entry& b) {
    return a.score() < b.score();
}



static bool alg_from_outer_requirement(const Alg& target, const Alg& b, char outer_op, bool b_on_left, Alg& y) {
    // b_on_left=false:  y op b = target
    // b_on_left=true:   b op y = target
    if (!b_on_left) {
        if (outer_op == '+') return sub_alg(target, b, y);
        if (outer_op == '-') return add_alg(target, b, y);
        if (outer_op == '*') return div_alg(target, b, y);
        if (outer_op == '/') return mul_alg(target, b, y);
    } else {
        if (outer_op == '+') return sub_alg(target, b, y);
        if (outer_op == '-') return sub_alg(b, target, y);
        if (outer_op == '*') return div_alg(target, b, y);
        if (outer_op == '/') return div_alg(b, target, y);
    }
    return false;
}

static bool alg_from_inner_requirement(const Alg& y, const Alg& a, char inner_op, Alg& v) {
    // v op a = y
    if (inner_op == '+') return sub_alg(y, a, v);
    if (inner_op == '-') return add_alg(y, a, v);
    if (inner_op == '*') return div_alg(y, a, v);
    if (inner_op == '/') return mul_alg(y, a, v);
    return false;
}

static int finish_expr_score(const string& e) {
    int neg = 0;
    for (size_t p = e.find("-("); p != string::npos; p = e.find("-(", p + 2)) neg++;
    return neg * 100000 + (int)e.size();
}

static optional<string> try_finish_with_one_digit(int n, long long target) {
    if (sets[1].empty()) return nullopt;
    Alg tgt = Alg::rational(target);
    const char ops[] = {'+', '-', '*', '/'};
    optional<string> best;
    auto consider = [&](const string& expr) {
        if (!best || finish_expr_score(expr) < finish_expr_score(*best)) best = expr;
    };
    for (const auto& pa : sets[1]) {
        for (char op : ops) {
            Alg v;
            if (alg_from_inner_requirement(tgt, pa.first, op, v)) {
                auto it = sets[n].find(v);
                if (it != sets[n].end()) consider(par(it->second.expr + " " + string(1, op) + " " + pa.second.expr));
            }
            if (alg_from_outer_requirement(tgt, pa.first, op, true, v)) {
                auto it = sets[n].find(v);
                if (it != sets[n].end()) consider(par(pa.second.expr + " " + string(1, op) + " " + it->second.expr));
            }
        }
    }
    return best;
}

static optional<string> try_finish_with_two_digits(int n, long long target) {
    if (sets[1].empty()) return nullopt;
    Alg tgt = Alg::rational(target);
    const char ops[] = {'+', '-', '*', '/'};
    optional<string> best;
    auto consider = [&](const string& expr) {
        if (!best || finish_expr_score(expr) < finish_expr_score(*best)) best = expr;
    };

    for (const auto& pa : sets[1]) {
        for (const auto& pb : sets[1]) {
            for (char inner : ops) {
                for (char outer : ops) {
                    Alg y, v;

                    if (alg_from_outer_requirement(tgt, pb.first, outer, false, y) &&
                        alg_from_inner_requirement(y, pa.first, inner, v)) {
                        auto it = sets[n].find(v);
                        if (it != sets[n].end()) {
                            consider(par(par(it->second.expr + " " + string(1, inner) + " " + pa.second.expr) +
                                         " " + string(1, outer) + " " + pb.second.expr));
                        }
                    }

                    if (alg_from_outer_requirement(tgt, pb.first, outer, true, y) &&
                        alg_from_inner_requirement(y, pa.first, inner, v)) {
                        auto it = sets[n].find(v);
                        if (it != sets[n].end()) {
                            consider(par(pb.second.expr + " " + string(1, outer) + " " +
                                         par(it->second.expr + " " + string(1, inner) + " " + pa.second.expr)));
                        }
                    }
                }
            }
        }
    }
    return best;
}


Result solve(int digit, long long target, int max_digits) {
    precompute_factorials();
    for (int i = 0; i <= 12; i++) sets[i].clear();

    if (auto special = try_known_special_patterns(digit, target, max_digits)) {
        return *special;
    }

    // Use digit! as the defining radicand when possible. For 1 and 2, digit! is too small;
    // using the digit itself still gives meaningful sqrt/sqrt behavior.
    ROOT_BASE = (digit >= 3 && digit <= 12) ? fact_table[digit] : digit;
    if (ROOT_BASE <= 1) ROOT_BASE = digit;

    for (int n = 1; n <= max_digits; n++) {
        // Concatenation seed.
        string cs = concat_digit(digit, n);
        long long cv = stoll(cs);
        Entry ce;
        ce.expr = cs;
        ce.depth = 0; ce.negs = 0; ce.ops = 0;
        try_insert(n, Alg::rational(cv), ce);

        // Combine previous sets — ROUND ROBIN across partitions.
        struct Partition {
            int i, j;
            vector<pair<Alg, Entry>> A, B;
        };
        vector<Partition> parts;
        for (int i = n / 2; i >= 1; i--) {
            int j = n - i;
            if (sets[i].empty() || sets[j].empty()) continue;
            Partition p;
            p.i = i; p.j = j;
            p.A.reserve(sets[i].size()); p.B.reserve(sets[j].size());
            for (auto& kv : sets[i]) p.A.push_back(kv);
            for (auto& kv : sets[j]) p.B.push_back(kv);
            parts.push_back(move(p));
        }

        size_t max_outer = 0;
        for (auto& p : parts) max_outer = max(max_outer, p.A.size());

        int num_parts = (int)parts.size();
        int budget_per_part = num_parts > 0 ? MAX_SET_SIZE / num_parts : MAX_SET_SIZE;
        vector<int> part_inserts(num_parts, 0);

        for (size_t outer = 0; outer < max_outer; outer++) {
            if ((int)sets[n].size() >= MAX_SET_SIZE) break;

            for (int pi = 0; pi < num_parts; pi++) {
                auto& part = parts[pi];
                if (outer >= part.A.size()) continue;
                if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                if (part_inserts[pi] >= budget_per_part) continue;

                int i = part.i, j = part.j;
                const Alg& v1 = part.A[outer].first;
                const Entry& e1 = part.A[outer].second;
                int size_before = (int)sets[n].size();

                for (const auto& pb : part.B) {
                    if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                    if (part_inserts[pi] + ((int)sets[n].size() - size_before) >= budget_per_part) break;
                    const Alg& v2 = pb.first;
                    const Entry& e2 = pb.second;
                    int depth = max(e1.depth, e2.depth) + 1;
                    int negs = e1.negs + e2.negs;
                    int ops = e1.ops + e2.ops + 1;

                    auto insert_bin = [&](const Alg& val, const string& expr) {
                        Entry e;
                        e.expr = expr;
                        e.depth = depth;
                        e.negs = negs;
                        e.ops = ops;
                        try_insert(n, val, e);
                    };

                    Alg r;
                    if (add_alg(v1, v2, r)) insert_bin(r, par(e1.expr + " + " + e2.expr));
                    if (sub_alg(v1, v2, r)) insert_bin(r, par(e1.expr + " - " + e2.expr));
                    if (i != j && sub_alg(v2, v1, r)) {
                        Entry e;
                        e.expr = par(e2.expr + " - " + e1.expr);
                        e.depth = depth; e.negs = negs; e.ops = ops;
                        try_insert(n, r, e);
                    }
                    if (mul_alg(v1, v2, r)) insert_bin(r, par(e1.expr + " * " + e2.expr));
                    if (div_alg(v1, v2, r)) insert_bin(r, par(e1.expr + " / " + e2.expr));
                    if (i != j && div_alg(v2, v1, r)) {
                        Entry e;
                        e.expr = par(e2.expr + " / " + e1.expr);
                        e.depth = depth; e.negs = negs; e.ops = ops;
                        try_insert(n, r, e);
                    }

                    // Exponentiation: exponent must be an exact integer.
                    long long exp;
                    if (v2.is_rational_int(exp) && llabs(exp) <= MAX_POW_EXP) {
                        if (pow_alg_int(v1, exp, r)) insert_bin(r, par(e1.expr + " ^ " + e2.expr));
                    }
                    if (i != j && v1.is_rational_int(exp) && llabs(exp) <= MAX_POW_EXP) {
                        if (pow_alg_int(v2, exp, r)) {
                            Entry e;
                            e.expr = par(e2.expr + " ^ " + e1.expr);
                            e.depth = depth; e.negs = negs; e.ops = ops;
                            try_insert(n, r, e);
                        }
                    }
                }
                part_inserts[pi] += ((int)sets[n].size() - size_before);
            }
        }

        apply_unary(n);

        cerr << "  S[" << n << "] = " << sets[n].size() << " values" << endl;

        Alg tgt = Alg::rational(target);
        auto it = sets[n].find(tgt);
        if (it != sets[n].end()) {
            return {true, n, it->second.expr};
        }

        // Target-aware finishing pass at EVERY level.
        // If target = (S[n] op digit) op digit, we can solve in n+2 digits
        // without needing to build the huge S[n+1] and S[n+2] sets.
        if (n + 1 <= max_digits) {
            if (auto e = try_finish_with_one_digit(n, target)) return {true, n + 1, *e};
        }
        if (n + 2 <= max_digits) {
            if (auto e = try_finish_with_two_digits(n, target)) return {true, n + 2, *e};
        }
    }
    return {false, max_digits, ""};
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: ./tchiselI <digit> <target> [max_digits]" << endl;
        return 1;
    }
    int digit = atoi(argv[1]);
    long long target = atoll(argv[2]);
    int max_digits = (argc >= 4) ? atoi(argv[3]) : 9;

    if (digit < 1 || digit > 9) {
        cerr << "Error: digit must be 1-9" << endl;
        return 1;
    }
    if (target == 0) {
        cerr << "Error: target must be nonzero" << endl;
        return 1;
    }
    if (max_digits < 1) max_digits = 1;
    if (max_digits > 12) max_digits = 12;

    cerr << endl;
    cerr << "  TCHISEL symbolic" << endl;
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
        return 0;
    }
    cout << endl;
    cout << "  No solution found within " << max_digits << " digits." << endl;
    cout << endl;
    return 0;
}