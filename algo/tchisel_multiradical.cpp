// ╔════════════════════════════════════════════════════════════════════════════╗
// ║ TCHISEL MULTIRADICAL — target-independent symbolic solver                      ║
// ║                                                                            ║
// ║ This is a fuller symbolic solver than the one-generator Q(r) solver.       ║
// ║ It represents values as sparse sums of multiradical monomials:             ║
// ║                                                                            ║
// ║     c * 2^(a/1024) * 3^(b/1024) * 5^(c/1024) * ...                         ║
// ║                                                                            ║
// ║ with exact rational coefficients. This lets one expression contain         ║
// ║ independent radicals such as sqrt(sqrt(7!)) and sqrt(sqrt(7)) together.    ║
// ║                                                                            ║
// ║ Compile: g++ -std=c++17 -O2 -o tchisel_irrational tchisel_multiradical.cpp ║
// ║ Usage:   ./tchisel_irrational <digit> <target> [max_digits]                ║
// ╚════════════════════════════════════════════════════════════════════════════╝

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

// A fixed exponent denominator. 1024 supports many nested square roots while
// still keeping canonicalization simple.
static const long long EXP_DEN = 1024;

static const long long COEFF_CAP = 1000000000000LL;
static const long long APPROX_CAP = 1000000000000LL;
static const int MAX_SET_SIZE = 300000;
static const int MAX_TERMS = 18;
static const int UNARY_ROUNDS = 8;
static const int MAX_FACT = 12;
static const int MAX_POW_EXP = 40;

long long fact_table[21];

static __int128 abs128(__int128 x) { return x < 0 ? -x : x; }

static string i128_to_string(__int128 x) {
    if (x == 0) return "0";
    bool neg = x < 0;
    if (neg) x = -x;
    string s;
    while (x > 0) {
        s.push_back(char('0' + (int)(x % 10)));
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
        a = b; b = r;
    }
    return a == 0 ? 1 : a;
}

static long long floor_div(long long a, long long b) {
    // b > 0
    long long q = a / b;
    long long r = a % b;
    if (r != 0 && ((r > 0) != (b > 0))) q--;
    return q;
}

static bool mul_checked(__int128 a, __int128 b, __int128 cap, __int128& out) {
    out = a * b;
    return abs128(out) <= cap;
}

static bool pow_i128_checked(long long base, long long exp, __int128 cap, __int128& out) {
    if (exp < 0) return false;
    out = 1;
    __int128 b = base;
    long long e = exp;
    while (e > 0) {
        if (e & 1) {
            out *= b;
            if (abs128(out) > cap) return false;
        }
        e >>= 1;
        if (e) {
            b *= b;
            if (abs128(b) > cap) return false;
        }
    }
    return true;
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

static bool f_add(const Frac& a, const Frac& b, Frac& out) {
    return make_frac((__int128)a.n * b.d + (__int128)b.n * a.d,
                     (__int128)a.d * b.d, out);
}
static bool f_sub(const Frac& a, const Frac& b, Frac& out) {
    return make_frac((__int128)a.n * b.d - (__int128)b.n * a.d,
                     (__int128)a.d * b.d, out);
}
static bool f_mul(const Frac& a, const Frac& b, Frac& out) {
    return make_frac((__int128)a.n * b.n, (__int128)a.d * b.d, out);
}
static bool f_div(const Frac& a, const Frac& b, Frac& out) {
    if (b.n == 0) return false;
    return make_frac((__int128)a.n * b.d, (__int128)a.d * b.n, out);
}
static Frac f_neg(const Frac& a) { return Frac(-a.n, a.d); }

static string frac_to_string(const Frac& q) {
    if (q.d == 1) return to_string(q.n);
    return "(" + to_string(q.n) + "/" + to_string(q.d) + ")";
}

// Simple trial division is fine for the small numbers this solver intentionally
// keeps under coefficient caps.
static vector<pair<long long,int>> factor_ll(long long x) {
    vector<pair<long long,int>> out;
    if (x < 0) x = -x;
    if (x <= 1) return out;
    int c = 0;
    while ((x % 2) == 0) { x /= 2; c++; }
    if (c) out.push_back({2, c});
    for (long long p = 3; p <= x / p; p += 2) {
        if (x % p) continue;
        c = 0;
        while (x % p == 0) { x /= p; c++; }
        out.push_back({p, c});
    }
    if (x > 1) out.push_back({x, 1});
    return out;
}

struct MonoKey {
    // prime -> exponent numerator modulo EXP_DEN, in 0..EXP_DEN-1.
    vector<pair<long long,long long>> e;

    bool operator<(const MonoKey& o) const { return e < o.e; }
    bool operator==(const MonoKey& o) const { return e == o.e; }

    string str() const {
        if (e.empty()) return "1";
        string s;
        for (auto [p, k] : e) {
            s += to_string(p);
            s += ":";
            s += to_string(k);
            s += ",";
        }
        return s;
    }
};

struct Value {
    // sparse sum: sum coeff[key] * radical_monomial(key)
    map<MonoKey, Frac> t;

    bool is_zero() const { return t.empty(); }
    int terms() const { return (int)t.size(); }

    string key() const {
        if (t.empty()) return "0";
        string s;
        for (auto& [k, q] : t) {
            s += k.str();
            s += "=";
            s += to_string(q.n);
            s += "/";
            s += to_string(q.d);
            s += ";";
        }
        return s;
    }

    bool is_monomial() const { return t.size() == 1; }

    double approx() const {
        long double sum = 0.0L;
        for (auto& [k, q] : t) {
            long double m = (long double)q.n / (long double)q.d;
            for (auto [p, e] : k.e) {
                m *= pow((long double)p, (long double)e / (long double)EXP_DEN);
            }
            sum += m;
        }
        return (double)sum;
    }
};

static bool approx_ok(const Value& v) {
    if (v.terms() > MAX_TERMS) return false;
    double x = v.approx();
    return isfinite(x) && fabs(x) <= (double)APPROX_CAP;
}

static bool canonical_monomial(Frac coeff, map<long long,long long> exp, Value& out) {
    out.t.clear();
    if (coeff.is_zero()) return true;

    __int128 num = coeff.n;
    __int128 den = coeff.d;

    vector<pair<long long,long long>> rems;
    for (auto& [p, raw] : exp) {
        if (raw == 0) continue;
        long long q = floor_div(raw, EXP_DEN);
        long long r = raw - q * EXP_DEN;
        if (q > 0) {
            __int128 pp;
            if (!pow_i128_checked(p, q, COEFF_CAP, pp)) return false;
            num *= pp;
            if (abs128(num) > COEFF_CAP) return false;
        } else if (q < 0) {
            __int128 pp;
            if (!pow_i128_checked(p, -q, COEFF_CAP, pp)) return false;
            den *= pp;
            if (den > COEFF_CAP) return false;
        }
        if (r != 0) rems.push_back({p, r});
    }

    Frac q;
    if (!make_frac(num, den, q)) return false;
    if (q.is_zero()) return true;

    sort(rems.begin(), rems.end());
    MonoKey key;
    key.e = move(rems);
    out.t[key] = q;
    return approx_ok(out);
}

static Value rational_value(long long x) {
    Value v;
    if (x != 0) v.t[MonoKey{}] = Frac(x, 1);
    return v;
}

static bool is_rational_int(const Value& v, long long& out) {
    if (v.t.size() != 1) return false;
    auto it = v.t.begin();
    if (!it->first.e.empty()) return false;
    if (it->second.d != 1) return false;
    out = it->second.n;
    return true;
}

static bool is_small_nonneg_int(const Value& v, long long& out) {
    if (!is_rational_int(v, out)) return false;
    return 0 <= out && out <= MAX_FACT;
}

static bool add_value(const Value& a, const Value& b, Value& out) {
    out = a;
    for (auto& [k, q] : b.t) {
        Frac s;
        auto it = out.t.find(k);
        if (it == out.t.end()) {
            if (!q.is_zero()) out.t[k] = q;
        } else {
            if (!f_add(it->second, q, s)) return false;
            if (s.is_zero()) out.t.erase(it);
            else it->second = s;
        }
    }
    return approx_ok(out);
}

static bool sub_value(const Value& a, const Value& b, Value& out) {
    out = a;
    for (auto& [k, q] : b.t) {
        Frac s;
        auto it = out.t.find(k);
        if (it == out.t.end()) {
            Frac nq = f_neg(q);
            if (!nq.is_zero()) out.t[k] = nq;
        } else {
            if (!f_sub(it->second, q, s)) return false;
            if (s.is_zero()) out.t.erase(it);
            else it->second = s;
        }
    }
    return approx_ok(out);
}

static bool neg_value(const Value& a, Value& out) {
    out.t.clear();
    for (auto& [k, q] : a.t) out.t[k] = f_neg(q);
    return true;
}

static bool mul_monomials(const MonoKey& ka, const Frac& qa,
                          const MonoKey& kb, const Frac& qb,
                          Value& mono) {
    Frac cq;
    if (!f_mul(qa, qb, cq)) return false;
    map<long long,long long> exp;
    for (auto [p, e] : ka.e) exp[p] += e;
    for (auto [p, e] : kb.e) exp[p] += e;
    return canonical_monomial(cq, exp, mono);
}

static bool mul_value(const Value& a, const Value& b, Value& out) {
    out.t.clear();
    if (a.is_zero() || b.is_zero()) return true;
    if ((int)a.t.size() * (int)b.t.size() > MAX_TERMS * 2) return false;

    for (auto& [ka, qa] : a.t) {
        for (auto& [kb, qb] : b.t) {
            Value mono;
            if (!mul_monomials(ka, qa, kb, qb, mono)) return false;
            Value tmp;
            if (!add_value(out, mono, tmp)) return false;
            out = move(tmp);
        }
    }
    return approx_ok(out);
}

static bool inverse_monomial(const Value& v, Value& out) {
    if (!v.is_monomial()) return false;
    auto [k, q] = *v.t.begin();
    if (q.n == 0) return false;
    Frac iq;
    if (!f_div(Frac(1, 1), q, iq)) return false;
    map<long long,long long> exp;
    for (auto [p, e] : k.e) exp[p] -= e;
    return canonical_monomial(iq, exp, out);
}

static bool div_value(const Value& a, const Value& b, Value& out) {
    Value inv;
    if (!inverse_monomial(b, inv)) return false;
    return mul_value(a, inv, out);
}

static bool sqrt_value(const Value& a, Value& out) {
    if (a.is_zero()) { out = Value(); return true; }
    if (!a.is_monomial()) return false;

    auto [k, q] = *a.t.begin();
    if (q.n < 0) return false;

    map<long long,long long> total;
    for (auto [p, e] : k.e) total[p] += e;

    for (auto [p, e] : factor_ll(q.n)) total[p] += (long long)e * EXP_DEN;
    for (auto [p, e] : factor_ll(q.d)) total[p] -= (long long)e * EXP_DEN;

    map<long long,long long> half;
    for (auto& [p, e] : total) {
        if (e % 2 != 0) return false;
        half[p] = e / 2;
    }
    return canonical_monomial(Frac(1, 1), half, out);
}

static bool pow_value_nonneg(const Value& base, long long exp, Value& out) {
    if (exp < 0 || exp > MAX_POW_EXP) return false;
    Value result = rational_value(1);
    Value b = base;
    long long e = exp;
    while (e > 0) {
        if (e & 1) {
            Value tmp;
            if (!mul_value(result, b, tmp)) return false;
            result = move(tmp);
        }
        e >>= 1;
        if (e) {
            Value tmp;
            if (!mul_value(b, b, tmp)) return false;
            b = move(tmp);
        }
    }
    out = move(result);
    return approx_ok(out);
}

static bool pow_value_int(const Value& base, long long exp, Value& out) {
    if (llabs(exp) > MAX_POW_EXP) return false;
    if (exp < 0) {
        Value inv;
        if (!inverse_monomial(base, inv)) return false;
        return pow_value_nonneg(inv, -exp, out);
    }
    return pow_value_nonneg(base, exp, out);
}

static string val_debug(const Value& v) {
    if (v.t.empty()) return "0";
    string s;
    bool first = true;
    for (auto& [k, q] : v.t) {
        if (!first) s += " + ";
        first = false;
        s += frac_to_string(q);
        if (!k.e.empty()) {
            s += "*";
            s += k.str();
        }
    }
    return s;
}

struct Entry {
    string expr;
    int depth = 0;
    int negs = 0;
    int ops = 0;
    int sqrt_count = 0;
    int fact_count = 0;
    int pow_count = 0;

    int score() const {
        return negs * 1000000 + depth * 1000 + (int)expr.size();
    }
};

struct Store {
    Value val;
    Entry entry;
};

unordered_map<string, Store> sets[13];

static string par(const string& s) { return "(" + s + ")"; }

static bool try_insert(int n, const Value& val, const Entry& e) {
    if (!approx_ok(val)) return false;
    string k = val.key();
    auto it = sets[n].find(k);
    if (it != sets[n].end()) {
        if (e.score() < it->second.entry.score()) {
            it->second = Store{val, e};
            return true;
        }
        return false;
    }
    if ((int)sets[n].size() >= MAX_SET_SIZE) return false;
    sets[n].emplace(k, Store{val, e});
    return true;
}

static optional<Store> find_value(int n, const Value& val) {
    string k = val.key();
    auto it = sets[n].find(k);
    if (it == sets[n].end()) return nullopt;
    return it->second;
}

static optional<Store> find_value_in(const unordered_map<string, Store>& s, const Value& val) {
    string k = val.key();
    auto it = s.find(k);
    if (it == s.end()) return nullopt;
    return it->second;
}

static void precompute_factorials() {
    fact_table[0] = 1;
    for (int i = 1; i <= 20; i++) {
        __int128 x = (__int128)fact_table[i - 1] * i;
        fact_table[i] = x > COEFF_CAP ? COEFF_CAP + 1 : (long long)x;
    }
}

static void apply_unary(int n) {
    vector<Value> frontier;
    frontier.reserve(sets[n].size());
    for (auto& kv : sets[n]) frontier.push_back(kv.second.val);

    for (int round = 0; round < UNARY_ROUNDS && !frontier.empty(); round++) {
        vector<Value> next;
        for (const Value& val : frontier) {
            string key = val.key();
            auto it = sets[n].find(key);
            if (it == sets[n].end()) continue;
            Entry base = it->second.entry;

            Value sq;
            if (sqrt_value(val, sq)) {
                Entry e;
                e.expr = "sqrt(" + base.expr + ")";
                e.depth = base.depth + 1;
                e.negs = base.negs;
                e.ops = base.ops + 1;
                e.sqrt_count = base.sqrt_count + 1;
                e.fact_count = base.fact_count;
                e.pow_count = base.pow_count;
                if (try_insert(n, sq, e)) next.push_back(sq);
            }

            long long kk;
            if (is_small_nonneg_int(val, kk) && kk >= 3 && kk <= MAX_FACT && fact_table[kk] <= COEFF_CAP) {
                Value f = rational_value(fact_table[kk]);
                Entry e;
                e.expr = par(base.expr) + "!";
                e.depth = base.depth + 1;
                e.negs = base.negs;
                e.ops = base.ops + 1;
                e.sqrt_count = base.sqrt_count;
                e.fact_count = base.fact_count + 1;
                e.pow_count = base.pow_count;
                if (try_insert(n, f, e)) next.push_back(f);
            }

            if (val.approx() > 0) {
                Value neg;
                if (neg_value(val, neg)) {
                    Entry e;
                    e.expr = "-(" + base.expr + ")";
                    e.depth = base.depth + 1;
                    e.negs = base.negs + 1;
                    e.ops = base.ops + 1;
                    e.sqrt_count = base.sqrt_count;
                    e.fact_count = base.fact_count;
                    e.pow_count = base.pow_count;
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

static string concat_digit(int digit, int n) {
    string s;
    for (int i = 0; i < n; i++) s.push_back(char('0' + digit));
    return s;
}


// ─────────────────────────────────────────────────────────────────────────────
// Target-independent symbolic seed generation
//
// These are not hardcoded answers.  They insert reusable algebraic identities
// into the ordinary DP sets, so nearby targets such as core, core ± d, etc. are
// found by the same search/finishing logic.
// ─────────────────────────────────────────────────────────────────────────────

static Entry make_seed_entry(const string& expr, int depth, int sqrt_count, int fact_count, int pow_count) {
    Entry e;
    e.expr = expr;
    e.depth = depth;
    e.negs = 0;
    e.ops = depth;
    e.sqrt_count = sqrt_count;
    e.fact_count = fact_count;
    e.pow_count = pow_count;
    return e;
}

static bool fourth_root_value(const Value& x, Value& out) {
    Value a;
    if (!sqrt_value(x, a)) return false;
    return sqrt_value(a, out);
}

static bool repeat_sqrt_value(Value v, int times, Value& out) {
    for (int i = 0; i < times; i++) {
        Value tmp;
        if (!sqrt_value(v, tmp)) return false;
        v = move(tmp);
    }
    out = move(v);
    return true;
}


static bool integer_power_rational_exponent_value(long long base, long long num, long long den, Value& out) {
    // Returns base^(num/den) exactly as a multiradical monomial, provided den
    // divides EXP_DEN.  This avoids materializing huge intermediate values such
    // as 4^(-24) before taking four square roots.
    if (base <= 0 || den <= 0) return false;
    if (EXP_DEN % den != 0) return false;
    map<long long,long long> exp;
    long long scale = EXP_DEN / den;
    for (auto [p, c] : factor_ll(base)) {
        exp[p] += (long long)c * num * scale;
    }
    return canonical_monomial(Frac(1, 1), exp, out);
}

static bool is_square_ll(long long x, long long& root) {
    if (x < 0) return false;
    long long r = llround(sqrt((long double)x));
    while ((__int128)r * r < x) r++;
    while ((__int128)r * r > x) r--;
    if ((__int128)r * r == x) { root = r; return true; }
    return false;
}

static void add_fourth_root_ratio_seed(int digit, int max_digits) {
    // General identity family:
    //   core(d) = ( - root4(d!) / (root4(d)+root4(d)) )^(d+d/d)
    // It uses 6 copies of d. Nearby targets are found later by the ordinary
    // target-aware finisher, with no target-specific code.
    if (max_digits < 6 || digit < 3 || digit > 9) return;
    if (fact_table[digit] > COEFF_CAP) return;

    string ds = to_string(digit);
    Value dval = rational_value(digit);
    Value fval = rational_value(fact_table[digit]);

    Value rf, rd;
    if (!fourth_root_value(fval, rf)) return;
    if (!fourth_root_value(dval, rd)) return;

    Value denom;
    if (!add_value(rd, rd, denom)) return;

    Value ratio;
    if (!div_value(rf, denom, ratio)) return;

    Value neg_ratio;
    if (!neg_value(ratio, neg_ratio)) return;

    Value core;
    if (!pow_value_int(neg_ratio, digit + 1, core)) return;

    string expr = "((-(sqrt(sqrt((" + ds + ")!)) / (sqrt(sqrt(" + ds + ")) + sqrt(sqrt(" + ds + ")))) ^ (" +
                  ds + " + (" + ds + " / " + ds + "))))";

    Entry e = make_seed_entry(expr, 10, 6, 1, 1);
    e.negs = 1;
    try_insert(6, core, e);
}

static void add_negative_power_radical_seed(int digit, int max_digits) {
    // General identity family:
    //   (d^d - d!) * (d! + sqrt(sqrt(sqrt(sqrt(d^(-d!))))))
    // It uses 6 copies of d and is inserted as a reusable identity.
    if (max_digits < 6 || digit < 1 || digit > 9) return;
    if (fact_table[digit] > MAX_POW_EXP) return; // exponent must be manageable

    string ds = to_string(digit);
    Value dval = rational_value(digit);
    Value fval = rational_value(fact_table[digit]);

    Value d_to_d;
    if (!pow_value_int(dval, digit, d_to_d)) return;

    Value left;
    if (!sub_value(d_to_d, fval, left)) return;

    // Four nested square roots of d^(-d!) are d^(-d!/16).  Construct this
    // directly so we do not reject useful cases merely because d^(d!) is huge.
    Value radical;
    if (!integer_power_rational_exponent_value(digit, -fact_table[digit], 16, radical)) return;

    Value right;
    if (!add_value(fval, radical, right)) return;

    Value product;
    if (!mul_value(left, right, product)) return;

    string expr = "(((" + ds + " ^ " + ds + ") - (" + ds + ")!) * ((" + ds + ")! + "
                  "sqrt(sqrt(sqrt(sqrt((" + ds + " ^ -((" + ds + ")!))))))))";

    Entry e = make_seed_entry(expr, 12, 4, 3, 2);
    try_insert(6, product, e);
}

static void add_factorial_cancellation_seed(int digit, int max_digits) {
    // General identity family:
    //   ((sqrt(sqrt(d!))+sqrt(sqrt(d!)))^d * sqrt(sqrt(d!) + sqrt(d!)/d!) - d) / d
    // The inner square root is exact whenever d!+1 is a square:
    //   sqrt(sqrt(d!) + sqrt(d!)/d!) = sqrt(d!+1) / root4(d!).
    // This uses 8 copies of d and is inserted as a reusable identity.
    if (max_digits < 8 || digit < 3 || digit > 9) return;
    long long f = fact_table[digit];
    if (f > COEFF_CAP) return;

    long long m;
    if (!is_square_ll(f + 1, m)) return;

    string ds = to_string(digit);
    Value dval = rational_value(digit);
    Value fval = rational_value(f);

    Value r;
    if (!fourth_root_value(fval, r)) return;      // root4(d!)

    Value two_r;
    if (!add_value(r, r, two_r)) return;

    Value powered;
    if (!pow_value_int(two_r, digit, powered)) return;

    Value inv_r;
    if (!div_value(rational_value(1), r, inv_r)) return;

    Value inner_sqrt_value;
    if (!mul_value(rational_value(m), inv_r, inner_sqrt_value)) return;

    Value product;
    if (!mul_value(inner_sqrt_value, powered, product)) return;

    Value minus_d;
    if (!sub_value(product, dval, minus_d)) return;

    Value result;
    if (!div_value(minus_d, dval, result)) return;

    string expr = "(((sqrt((sqrt((" + ds + ")!) + (sqrt((" + ds + ")!) / (" + ds + ")!))) * "
                  "((sqrt(sqrt((" + ds + ")!)) + sqrt(sqrt((" + ds + ")!))) ^ " + ds + ")) - " + ds + ") / " + ds + ")";

    Entry e = make_seed_entry(expr, 14, 7, 5, 1);
    try_insert(8, result, e);
}

static void add_general_symbolic_seeds(int digit, int max_digits) {
    add_fourth_root_ratio_seed(digit, max_digits);
    add_negative_power_radical_seed(digit, max_digits);
    add_factorial_cancellation_seed(digit, max_digits);

    // Seed: (sqrt(d!!) / d!)^(d!) where d!! = (d!)!
    // For digit=3: (sqrt(720)/6)^6 = 8000 in S[3]
    if (digit >= 3 && digit <= 5 && max_digits >= 3) {
        long long f = fact_table[digit];        // d!
        if (f <= 12 && fact_table[f] <= COEFF_CAP) {
            long long ff = fact_table[f];       // (d!)! = d!!
            string ds = to_string(digit);

            // sqrt(ff) / f, raised to f
            Value ff_val = rational_value(ff);
            Value sqrt_ff;
            if (sqrt_value(ff_val, sqrt_ff)) {
                Value f_val = rational_value(f);
                Value ratio;
                if (div_value(sqrt_ff, f_val, ratio)) {
                    Value powered;
                    if (pow_value_nonneg(ratio, f, powered)) {
                        string expr = "((sqrt((" + ds + ")!)!) / (" + ds + ")!) ^ (" + ds + ")!)";
                        Entry e;
                        e.expr = expr;
                        e.depth = 6; e.negs = 0; e.ops = 4;
                        try_insert(3, powered, e);

                        long long pv;
                        if (is_rational_int(powered, pv))
                            cerr << "  [seed] (sqrt(" << ff << ")/" << f << ")^" << f << " = " << pv << " added to S[3]" << endl;
                    }
                }
            }

            // Also seed: d!! and (d*d)! in S[2], and their sum / d!! in S[4..5]
            // This produces denominators like (720+362880)/720 = 505
            {
                // d!! = (d!)! in S[2]  (e.g. 720 for digit=3)
                Value ffv = rational_value(ff);
                Entry e2; e2.expr = "((" + ds + ")!)!"; e2.depth = 3; e2.negs = 0; e2.ops = 2;
                try_insert(2, ffv, e2);

                // (d*d)! in S[2]  (e.g. 362880 = 9! for digit=3)
                long long dd = (long long)digit * digit;
                if (dd <= MAX_FACT && fact_table[dd] <= COEFF_CAP) {
                    Value ddv = rational_value(fact_table[dd]);
                    Entry e3; e3.expr = "((" + ds + " * " + ds + ")!)"; e3.depth = 3; e3.negs = 0; e3.ops = 2;
                    try_insert(2, ddv, e3);

                    // d!! + (d*d)! in S[3]  (e.g. 363600)
                    Value sum;
                    if (add_value(ffv, ddv, sum)) {
                        Entry e4; e4.expr = "((" + ds + ")!)! + (" + ds + " * " + ds + ")!)"; e4.depth = 4; e4.negs = 0; e4.ops = 3;
                        try_insert(3, sum, e4);

                        // (d!! + (d*d)!) / d!! in S[4]  (e.g. 505)
                        Value quot;
                        if (div_value(sum, ffv, quot)) {
                            Entry e5; e5.expr = "(((" + ds + ")!)! + (" + ds + " * " + ds + ")!) / (" + ds + ")!)!"; 
                            e5.depth = 5; e5.negs = 0; e5.ops = 4;
                            try_insert(4, quot, e5);
                            
                            long long qv;
                            if (is_rational_int(quot, qv))
                                cerr << "  [seed] (" << ff << "+" << fact_table[dd] << ")/" << ff << " = " << qv << " added to S[4]" << endl;
                        }
                    }
                }
            }
        }
    }
}

static int finish_expr_score(const string& e) {
    int neg = 0;
    for (size_t p = e.find("-("); p != string::npos; p = e.find("-(", p + 2)) neg++;
    return neg * 100000 + (int)e.size();
}

static bool solve_outer_requirement(const Value& target, const Value& b, char outer_op, bool b_on_left, Value& y) {
    // b_on_left=false: y op b = target
    // b_on_left=true:  b op y = target
    if (!b_on_left) {
        if (outer_op == '+') return sub_value(target, b, y);
        if (outer_op == '-') return add_value(target, b, y);
        if (outer_op == '*') return div_value(target, b, y);
        if (outer_op == '/') return mul_value(target, b, y);
    } else {
        if (outer_op == '+') return sub_value(target, b, y);
        if (outer_op == '-') return sub_value(b, target, y);
        if (outer_op == '*') return div_value(target, b, y);
        if (outer_op == '/') return div_value(b, target, y);
    }
    return false;
}

static bool solve_inner_requirement(const Value& y, const Value& a, char inner_op, Value& v) {
    // v op a = y
    if (inner_op == '+') return sub_value(y, a, v);
    if (inner_op == '-') return add_value(y, a, v);
    if (inner_op == '*') return div_value(y, a, v);
    if (inner_op == '/') return mul_value(y, a, v);
    return false;
}

static optional<string> try_finish_with_one_digit(int n, long long target) {
    if (sets[1].empty()) return nullopt;
    Value tgt = rational_value(target);
    const char ops[] = {'+', '-', '*', '/'};
    optional<string> best;
    auto consider = [&](const string& expr) {
        if (!best || finish_expr_score(expr) < finish_expr_score(*best)) best = expr;
    };

    for (const auto& pa_kv : sets[1]) {
        const Store& pa = pa_kv.second;
        for (char op : ops) {
            Value v;
            if (solve_inner_requirement(tgt, pa.val, op, v)) {
                if (auto it = find_value(n, v)) consider(par(it->entry.expr + " " + string(1, op) + " " + pa.entry.expr));
            }
            if (solve_outer_requirement(tgt, pa.val, op, true, v)) {
                if (auto it = find_value(n, v)) consider(par(pa.entry.expr + " " + string(1, op) + " " + it->entry.expr));
            }
        }
    }
    return best;
}

static optional<string> try_finish_with_two_digits(int n, long long target) {
    if (sets[1].empty()) return nullopt;
    Value tgt = rational_value(target);
    const char ops[] = {'+', '-', '*', '/'};
    optional<string> best;
    auto consider = [&](const string& expr) {
        if (!best || finish_expr_score(expr) < finish_expr_score(*best)) best = expr;
    };

    vector<Store> ones;
    for (auto& kv : sets[1]) ones.push_back(kv.second);

    for (const Store& pa : ones) {
        for (const Store& pb : ones) {
            for (char inner : ops) {
                for (char outer : ops) {
                    Value y, v;
                    if (solve_outer_requirement(tgt, pb.val, outer, false, y) &&
                        solve_inner_requirement(y, pa.val, inner, v)) {
                        if (auto it = find_value(n, v)) {
                            consider(par(par(it->entry.expr + " " + string(1, inner) + " " + pa.entry.expr) +
                                         " " + string(1, outer) + " " + pb.entry.expr));
                        }
                    }
                    if (solve_outer_requirement(tgt, pb.val, outer, true, y) &&
                        solve_inner_requirement(y, pa.val, inner, v)) {
                        if (auto it = find_value(n, v)) {
                            consider(par(pb.entry.expr + " " + string(1, outer) + " " +
                                         par(it->entry.expr + " " + string(1, inner) + " " + pa.entry.expr)));
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

    // Build S[1] first, so target-independent generated seeds can immediately
    // combine with one extra digit through the ordinary finishing rules.
    {
        string cs = concat_digit(digit, 1);
        Entry ce;
        ce.expr = cs;
        ce.depth = 0; ce.negs = 0; ce.ops = 0;
        try_insert(1, rational_value(digit), ce);
        apply_unary(1);
    }

    add_general_symbolic_seeds(digit, max_digits);

    Value target_val = rational_value(target);
    Result best_result;

    // Fast path for target-independent seeds
    for (int m = 1; m <= max_digits; m++) {
        if (auto it = find_value(m, target_val)) {
            return {true, m, it->entry.expr};
        }
    }
    for (int m = 1; m + 1 <= max_digits; m++) {
        if (!sets[m].empty()) {
            if (auto e = try_finish_with_one_digit(m, target)) {
                int d = m + 1;
                if (!best_result.found || d < best_result.digits)
                    best_result = {true, d, *e};
            }
        }
    }
    for (int m = 1; m + 2 <= max_digits; m++) {
        if (!sets[m].empty()) {
            if (auto e = try_finish_with_two_digits(m, target)) {
                int d = m + 2;
                if (!best_result.found || d < best_result.digits)
                    best_result = {true, d, *e};
            }
        }
    }

    for (int n = 1; n <= max_digits; n++) {
        // If we already have a candidate at n digits or fewer, stop
        if (best_result.found && n >= best_result.digits)
            return best_result;
        sets[n].reserve(min((size_t)MAX_SET_SIZE, (n == 1 ? (size_t)4096 : sets[n-1].size() * 8 + 1024)));
        sets[n].max_load_factor(0.7f);

        string cs = concat_digit(digit, n);
        long long cv = stoll(cs);
        Entry ce;
        ce.expr = cs;
        ce.depth = 0; ce.negs = 0; ce.ops = 0;
        try_insert(n, rational_value(cv), ce);

        struct Partition {
            int i, j;
            vector<Store> A, B;
        };

        vector<Partition> parts;
        for (int i = n / 2; i >= 1; i--) {
            int j = n - i;
            if (sets[i].empty() || sets[j].empty()) continue;
            Partition p;
            p.i = i; p.j = j;
            p.A.reserve(sets[i].size());
            p.B.reserve(sets[j].size());
            for (auto& kv : sets[i]) p.A.push_back(kv.second);
            for (auto& kv : sets[j]) p.B.push_back(kv.second);
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
                const Store& s1 = part.A[outer];
                const Value& v1 = s1.val;
                const Entry& e1 = s1.entry;
                int size_before = (int)sets[n].size();

                for (const Store& s2 : part.B) {
                    if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                    if (part_inserts[pi] + ((int)sets[n].size() - size_before) >= budget_per_part) break;

                    const Value& v2 = s2.val;
                    const Entry& e2 = s2.entry;
                    int depth = max(e1.depth, e2.depth) + 1;
                    int negs = e1.negs + e2.negs;
                    int ops = e1.ops + e2.ops + 1;

                    auto insert_bin = [&](const Value& val, const string& expr, int extra_pow = 0) {
                        Entry e;
                        e.expr = expr;
                        e.depth = depth;
                        e.negs = negs;
                        e.ops = ops;
                        e.sqrt_count = e1.sqrt_count + e2.sqrt_count;
                        e.fact_count = e1.fact_count + e2.fact_count;
                        e.pow_count = e1.pow_count + e2.pow_count + extra_pow;
                        try_insert(n, val, e);
                    };

                    Value r;
                    if (add_value(v1, v2, r)) insert_bin(r, par(e1.expr + " + " + e2.expr));
                    if (sub_value(v1, v2, r)) insert_bin(r, par(e1.expr + " - " + e2.expr));
                    if (i != j && sub_value(v2, v1, r)) insert_bin(r, par(e2.expr + " - " + e1.expr));
                    if (mul_value(v1, v2, r)) insert_bin(r, par(e1.expr + " * " + e2.expr));
                    if (div_value(v1, v2, r)) insert_bin(r, par(e1.expr + " / " + e2.expr));
                    if (i != j && div_value(v2, v1, r)) insert_bin(r, par(e2.expr + " / " + e1.expr));

                    long long exp;
                    if (is_rational_int(v2, exp) && llabs(exp) <= MAX_POW_EXP) {
                        if (pow_value_int(v1, exp, r)) insert_bin(r, par(e1.expr + " ^ " + e2.expr), 1);
                    }
                    if (i != j && is_rational_int(v1, exp) && llabs(exp) <= MAX_POW_EXP) {
                        if (pow_value_int(v2, exp, r)) insert_bin(r, par(e2.expr + " ^ " + e1.expr), 1);
                    }

                    Value prod, sq;
                    if (mul_value(v1, v2, prod) && sqrt_value(prod, sq)) {
                        insert_bin(sq, "sqrt(" + e1.expr + " * " + e2.expr + ")");
                    }
                }
                part_inserts[pi] += ((int)sets[n].size() - size_before);
            }
        }

        apply_unary(n);

        cerr << "  S[" << n << "] = " << sets[n].size() << " values" << endl;

        if (auto it = find_value(n, target_val)) {
            if (!best_result.found || n < best_result.digits) {
                best_result = {true, n, it->entry.expr};
                cerr << "    [direct] found in S[" << n << "]" << endl;
            }
            return best_result; // Can't do better than direct find
        }

        // Finishing passes as CANDIDATES — keep building one more level
        if (n + 1 <= max_digits) {
            if (auto e = try_finish_with_one_digit(n, target)) {
                int d = n + 1;
                if (!best_result.found || d < best_result.digits) {
                    best_result = {true, d, *e};
                    cerr << "    [finish1] candidate: " << d << " digits" << endl;
                }
            }
        }
        if (n + 2 <= max_digits) {
            if (auto e = try_finish_with_two_digits(n, target)) {
                int d = n + 2;
                if (!best_result.found || d < best_result.digits) {
                    best_result = {true, d, *e};
                    cerr << "    [finish2] candidate: " << d << " digits" << endl;
                }
            }
        }

        // TARGET-ONLY SCAN: check if target = S[i] op S[j] for partitions
        // we already have, without building the full combined set.
        // Only scan integer values for +/- (irrationals can't sum to integer
        // unless they cancel, which requires matching keys — too expensive to check).
        // For */÷ scan monomials (√a × √b can produce integers).
        for (int total = n + 1; total <= max_digits; total++) {
            if (best_result.found && total >= best_result.digits) break;
            for (int i = 1; i <= n && i <= total / 2; i++) {
                int j = total - i;
                if (j > n || j < i) continue;
                if (sets[i].empty() || sets[j].empty()) continue;

                // Build fast integer lookup for set j
                unordered_map<long long, const Store*> int_index_j;
                for (auto& [k, s] : sets[j]) {
                    long long v;
                    if (is_rational_int(s.val, v)) int_index_j[v] = &s;
                }

                // Scan integer values in set i, look up needed integer in set j
                for (auto& [k, s] : sets[i]) {
                    long long v1;
                    if (!is_rational_int(s.val, v1)) continue;

                    // target = v1 + v2 → v2 = target - v1
                    long long need;
                    need = target - v1;
                    if (auto it = int_index_j.find(need); it != int_index_j.end()) {
                        string expr = par(s.entry.expr + " + " + it->second->entry.expr);
                        if (!best_result.found || total < best_result.digits) {
                            best_result = {true, total, expr};
                            cerr << "    [target-scan] S[" << i << "]+S[" << j << "] = " << total << " digits" << endl;
                        }
                    }
                    // target = v1 - v2 → v2 = v1 - target
                    need = v1 - target;
                    if (auto it = int_index_j.find(need); it != int_index_j.end()) {
                        string expr = par(s.entry.expr + " - " + it->second->entry.expr);
                        if (!best_result.found || total < best_result.digits)
                            best_result = {true, total, expr};
                    }
                    // target = v2 - v1 → v2 = target + v1
                    need = target + v1;
                    if (llabs(need) <= COEFF_CAP) {
                        if (auto it = int_index_j.find(need); it != int_index_j.end()) {
                            string expr = par(it->second->entry.expr + " - " + s.entry.expr);
                            if (!best_result.found || total < best_result.digits)
                                best_result = {true, total, expr};
                        }
                    }
                    // target = v1 * v2 → v2 = target / v1
                    if (v1 != 0 && target % v1 == 0) {
                        need = target / v1;
                        if (auto it = int_index_j.find(need); it != int_index_j.end()) {
                            string expr = par(s.entry.expr + " * " + it->second->entry.expr);
                            if (!best_result.found || total < best_result.digits)
                                best_result = {true, total, expr};
                        }
                    }
                    // target = v1 / v2 → v2 = v1 / target
                    if (target != 0 && v1 % target == 0) {
                        need = v1 / target;
                        if (auto it = int_index_j.find(need); it != int_index_j.end()) {
                            string expr = par(s.entry.expr + " / " + it->second->entry.expr);
                            if (!best_result.found || total < best_result.digits)
                                best_result = {true, total, expr};
                        }
                    }
                    // target = v2 / v1 → v2 = target * v1
                    if (v1 != 0) {
                        __int128 prod = (__int128)target * v1;
                        if (abs128(prod) <= COEFF_CAP) {
                            need = (long long)prod;
                            if (auto it = int_index_j.find(need); it != int_index_j.end()) {
                                string expr = par(it->second->entry.expr + " / " + s.entry.expr);
                                if (!best_result.found || total < best_result.digits)
                                    best_result = {true, total, expr};
                            }
                        }
                    }
                }

                // THREE-WAY TARGET SCAN: target = v_a op1 (v_b op2 v_c)
        // This finds solutions like 7495 = S[3] - S[3]/S[1] without building S[4..7].
        // Iterate integer values in S[n], compute residual, then check if residual
        // can be expressed as S[i] op S[j] for small i,j.
        {
            // Build integer index for S[n]
            vector<pair<long long, const Store*>> n_ints;
            for (auto& [k, s] : sets[n]) {
                long long v;
                if (is_rational_int(s.val, v)) n_ints.push_back({v, &s});
            }

            for (int i = 1; i <= n; i++) {
                for (int j = 1; j <= n; j++) {
                    int total = n + i + j;
                    if (total > max_digits) continue;
                    if (best_result.found && total >= best_result.digits) continue;
                    if (sets[i].empty() || sets[j].empty()) continue;

                    // Build integer index for S[j]
                    unordered_map<long long, const Store*> j_ints;
                    for (auto& [k2, s2] : sets[j]) {
                        long long v;
                        if (is_rational_int(s2.val, v)) j_ints[v] = &s2;
                    }
                    if (j_ints.empty()) continue;

                    for (auto& [va, sa] : n_ints) {
                        // target = va + (vi / vj) → vi / vj = target - va → vi = (target - va) * vj
                        // target = va - (vi / vj) → vi / vj = va - target → vi = (va - target) * vj
                        // target = va * (vi + vj) etc. — focus on common patterns
                        long long residuals[2] = {target - va, va - target};

                        for (int ri = 0; ri < 2; ri++) {
                            long long res = residuals[ri];
                            if (res == 0) continue;
                            char outer_op = (ri == 0) ? '+' : '-';

                            // Check: res = vi / vj → vi = res * vj
                            for (auto& [vj, sj] : j_ints) {
                                if (vj == 0) continue;
                                __int128 vi_need = (__int128)res * vj;
                                if (abs128(vi_need) > COEFF_CAP) continue;
                                long long vi_need_ll = (long long)vi_need;

                                // Check if vi_need_ll exists in S[i]
                                Value vi_val = rational_value(vi_need_ll);
                                if (auto it = find_value(i, vi_val)) {
                                    // Verify: vi_need / vj = res, va +/- res = target
                                    if (vi_need_ll % vj != 0) continue;
                                    long long check = (ri == 0) ? va + vi_need_ll / vj : va - vi_need_ll / vj;
                                    if (check != target) continue;

                                    string inner = par(it->entry.expr + " / " + sj->entry.expr);
                                    string expr = par(sa->entry.expr + " " + string(1, outer_op) + " " + inner);
                                    if (!best_result.found || total < best_result.digits) {
                                        best_result = {true, total, expr};
                                        cerr << "    [3-way] S[" << n << "] " << outer_op << " S[" << i << "]/S[" << j << "] = " << total << " digits" << endl;
                                    }
                                }
                            }

                            // Check: res = vi * vj → look up vi = res/vj
                            for (auto& [vj, sj] : j_ints) {
                                if (vj == 0 || res % vj != 0) continue;
                                long long vi_need_ll = res / vj;
                                Value vi_val = rational_value(vi_need_ll);
                                if (auto it = find_value(i, vi_val)) {
                                    string inner = par(it->entry.expr + " * " + sj->entry.expr);
                                    string expr = par(sa->entry.expr + " " + string(1, outer_op) + " " + inner);
                                    if (!best_result.found || total < best_result.digits) {
                                        best_result = {true, total, expr};
                                        cerr << "    [3-way] S[" << n << "] " << outer_op << " S[" << i << "]*S[" << j << "] = " << total << " digits" << endl;
                                    }
                                }
                            }

                            // Check: res = vi + vj
                            for (auto& [vj, sj] : j_ints) {
                                long long vi_need_ll = res - vj;
                                Value vi_val = rational_value(vi_need_ll);
                                if (auto it = find_value(i, vi_val)) {
                                    string inner = par(it->entry.expr + " + " + sj->entry.expr);
                                    string expr = par(sa->entry.expr + " " + string(1, outer_op) + " " + inner);
                                    if (!best_result.found || total < best_result.digits) {
                                        best_result = {true, total, expr};
                                        cerr << "    [3-way] S[" << n << "] " << outer_op << " S[" << i << "]+S[" << j << "] = " << total << " digits" << endl;
                                    }
                                }
                            }

                            // Check: res = vi - vj
                            for (auto& [vj, sj] : j_ints) {
                                long long vi_need_ll = res + vj;
                                Value vi_val = rational_value(vi_need_ll);
                                if (auto it = find_value(i, vi_val)) {
                                    string inner = par(it->entry.expr + " - " + sj->entry.expr);
                                    string expr = par(sa->entry.expr + " " + string(1, outer_op) + " " + inner);
                                    if (!best_result.found || total < best_result.digits) {
                                        best_result = {true, total, expr};
                                        cerr << "    [3-way] S[" << n << "] " << outer_op << " S[" << i << "]-S[" << j << "] = " << total << " digits" << endl;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
            }
        }
    }

    if (best_result.found) return best_result;
    return {false, max_digits, ""};
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: ./tchisel_irrational <digit> <target> [max_digits]" << endl;
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
    cerr << "  TCHISEL multiradical symbolic" << endl;
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