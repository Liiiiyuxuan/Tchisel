// ╔══════════════════════════════════════════════════════════╗
// ║  TCHISEL — Tchisla Solver (v3 optimized)                ║
// ║  Compile: g++ -std=c++17 -O2 -o tchisel tchisel.cpp     ║
// ║  Usage:   ./tchisel <digit> <target> [max_digits]        ║
// ╚══════════════════════════════════════════════════════════╝

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
const int MAX_SET_SIZE = 5000000;

enum : int8_t {
    OP_CONCAT=0, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW,
    OP_SQRT, OP_SQRTMUL, OP_FACT, OP_NEG,
    OP_POWRT1, OP_POWRT2, OP_POWRT3, OP_POWRT4, OP_POWRT5
};

struct Deriv {
    long long arg_a, arg_b;
    int16_t neg_count, depth;
    int8_t op, set_a, set_b;
    int score() const { return neg_count * 1000 + depth; }
};

unordered_map<long long, Deriv> sets[13];
long long factorial_table[21];

void precompute_factorials() {
    factorial_table[0] = 1; factorial_table[1] = 1;
    for (int i = 2; i <= 20; i++) {
        factorial_table[i] = factorial_table[i-1] * i;
        if (factorial_table[i] > MAX_VAL) factorial_table[i] = MAX_VAL + 1;
    }
}

string reconstruct(long long val, int n) {
    auto it = sets[n].find(val);
    if (it == sets[n].end()) return "?";
    const Deriv& d = it->second;
    switch (d.op) {
    case OP_CONCAT: return to_string(val);
    case OP_ADD: return "(" + reconstruct(d.arg_a, d.set_a) + " + " + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_SUB: return "(" + reconstruct(d.arg_a, d.set_a) + " - " + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_MUL: return "(" + reconstruct(d.arg_a, d.set_a) + " * " + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_DIV: return "(" + reconstruct(d.arg_a, d.set_a) + " / " + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_POW: return "(" + reconstruct(d.arg_a, d.set_a) + " ^ " + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_SQRT: return "sqrt(" + reconstruct(d.arg_a, n) + ")";
    case OP_SQRTMUL: return "sqrt(" + reconstruct(d.arg_a, d.set_a) + " * " + reconstruct(d.arg_b, d.set_b) + ")";
    case OP_FACT: return "(" + reconstruct(d.arg_a, n) + ")!";
    case OP_NEG: return "-(" + reconstruct(d.arg_a, n) + ")";
    default:
        if (d.op >= OP_POWRT1 && d.op <= OP_POWRT5) {
            int k = d.op - OP_POWRT1 + 1;
            string inner = "(" + reconstruct(d.arg_a, d.set_a) + " ^ " + reconstruct(d.arg_b, d.set_b) + ")";
            for (int i = 0; i < k; i++) inner = "sqrt(" + inner + ")";
            return inner;
        }
        return "?";
    }
}

inline bool try_insert(int n, long long val, const Deriv& d) {
    if (val > MAX_VAL || val < -MAX_VAL) return false;
    auto it = sets[n].find(val);
    if (it != sets[n].end()) {
        if (d.score() < it->second.score()) { it->second = d; return true; }
        return false;
    }
    if ((int)sets[n].size() >= MAX_SET_SIZE) return false;
    sets[n].emplace(val, d);
    return true;
}

void apply_unary(int n) {
    vector<long long> frontier;
    frontier.reserve(sets[n].size());
    for (auto& [val, _] : sets[n]) frontier.push_back(val);

    for (int round = 0; round < 6 && !frontier.empty(); round++) {
        vector<long long> next;
        for (long long val : frontier) {
            auto it = sets[n].find(val);
            if (it == sets[n].end()) continue;
            int16_t nc = it->second.neg_count, dp = it->second.depth;

            if (val > 0) {
                long long sq = llround(sqrt((double)val));
                if (sq * sq == val && sq > 0 && sq < MAX_VAL) {
                    Deriv d{val, 0, nc, (int16_t)(dp+1), OP_SQRT, (int8_t)n, 0};
                    if (try_insert(n, sq, d)) next.push_back(sq);
                }
            }
            if (val >= 3 && val <= 12) {
                long long f = factorial_table[val];
                if (f <= MAX_VAL) {
                    Deriv d{val, 0, nc, (int16_t)(dp+1), OP_FACT, (int8_t)n, 0};
                    if (try_insert(n, f, d)) next.push_back(f);
                }
            }
            if (val > 0) {
                Deriv d{val, 0, (int16_t)(nc+1), (int16_t)(dp+1), OP_NEG, (int8_t)n, 0};
                if (try_insert(n, -val, d)) next.push_back(-val);
            }
        }
        frontier = move(next);
    }
}

long long safe_pow(long long base, long long exp) {
    if (exp < 0 || base <= 0) return 0;
    if (exp == 0) return 1;
    if (base == 1) return 1;
    if (base > 1000 || exp > 40) return 0;
    long long r = 1;
    for (long long i = 0; i < exp; i++) { r *= base; if (r > MAX_VAL) return 0; }
    return r;
}

// ─── Target-aware finishing ─────────────────────────────
// Avoid building S[n+1] / S[n+2] when target is 1-2 ops away from S[n].

string try_finish_one(int n, long long target, int digit) {
    long long d = digit;
    // target = x + d
    if (auto it = sets[n].find(target - d); it != sets[n].end())
        return "(" + reconstruct(it->first, n) + " + " + to_string(d) + ")";
    // target = x - d
    if (auto it = sets[n].find(target + d); it != sets[n].end())
        return "(" + reconstruct(it->first, n) + " - " + to_string(d) + ")";
    // target = d - x
    if (auto it = sets[n].find(d - target); it != sets[n].end())
        return "(" + to_string(d) + " - " + reconstruct(it->first, n) + ")";
    // target = x * d
    if (d != 0 && target % d == 0)
        if (auto it = sets[n].find(target / d); it != sets[n].end())
            return "(" + reconstruct(it->first, n) + " * " + to_string(d) + ")";
    // target = x / d
    if (long long x = target * d; llabs(x) <= MAX_VAL)
        if (auto it = sets[n].find(x); it != sets[n].end())
            return "(" + reconstruct(it->first, n) + " / " + to_string(d) + ")";
    // target = d / x
    if (target != 0 && d % target == 0)
        if (auto it = sets[n].find(d / target); it != sets[n].end())
            return "(" + to_string(d) + " / " + reconstruct(it->first, n) + ")";
    // target = x!
    for (int f = 3; f <= 12; f++)
        if (factorial_table[f] == target)
            if (auto it = sets[n].find(f); it != sets[n].end())
                return "(" + reconstruct(f, n) + ")!";
    // target = sqrt(x)
    if (long long x = target * target; x <= MAX_VAL)
        if (auto it = sets[n].find(x); it != sets[n].end())
            return "sqrt(" + reconstruct(x, n) + ")";
    // target = -x
    if (auto it = sets[n].find(-target); it != sets[n].end())
        return "-(" + reconstruct(-target, n) + ")";
    return "";
}

string try_finish_two(int n, long long target, int digit) {
    long long d = digit;
    struct { const char* fmt; long long x; } checks[] = {
        {"((%s + %d) / %d)", target * d - d},
        {"((%s - %d) / %d)", target * d + d},
        {"((%s * %d) + %d)", d != 0 && (target - d) % d == 0 ? (target - d) / d : MAX_VAL + 1},
        {"((%s * %d) - %d)", d != 0 && (target + d) % d == 0 ? (target + d) / d : MAX_VAL + 1},
        {"((%s / %d) + %d)", (target - d) * d},
        {"((%s / %d) - %d)", (target + d) * d},
        {"((%s + %d) * %d)", d != 0 && target % d == 0 ? target / d - d : MAX_VAL + 1},
        {"((%s - %d) * %d)", d != 0 && target % d == 0 ? target / d + d : MAX_VAL + 1},
    };
    for (auto& [fmt, x] : checks) {
        if (llabs(x) <= MAX_VAL) {
            auto it = sets[n].find(x);
            if (it != sets[n].end()) {
                string inner = reconstruct(x, n);
                // Build the expression from the format hint
                char buf[512];
                snprintf(buf, sizeof(buf), fmt, inner.c_str(), (int)d, (int)d);
                return string(buf);
            }
        }
    }
    return "";
}

// ─── Solver ─────────────────────────────────────────────

struct Result { bool found; int digits; string expression; };

Result solve(int digit, long long target, int max_digits) {
    precompute_factorials();
    for (int i = 0; i <= 12; i++) sets[i].clear();

    for (int n = 1; n <= max_digits; n++) {
        // Pre-reserve to avoid rehashing (biggest single speedup)
        size_t prev = n > 1 ? sets[n-1].size() : 0;
        size_t expected = min((size_t)MAX_SET_SIZE, max(prev * 8, (size_t)1024));
        sets[n].reserve(expected);
        sets[n].max_load_factor(0.7f);

        long long concat = 0;
        for (int k = 0; k < n; k++) concat = concat * 10 + digit;
        Deriv cd{0, 0, 0, 0, OP_CONCAT, 0, 0};
        try_insert(n, concat, cd);

        // Round-robin partitions with per-partition budgets
        struct Part {
            int i, j;
            vector<pair<long long, Deriv>> ai, bj;
        };
        vector<Part> parts;
        for (int i = 1; i <= n / 2; i++) {
            int j = n - i;
            if (sets[i].empty() || sets[j].empty()) continue;
            Part p;
            p.i = i; p.j = j;
            p.ai.assign(sets[i].begin(), sets[i].end());
            p.bj.assign(sets[j].begin(), sets[j].end());
            parts.push_back(move(p));
        }

        size_t max_outer = 0;
        for (auto& p : parts) max_outer = max(max_outer, p.ai.size());
        int np = (int)parts.size();
        int budget = np > 0 ? MAX_SET_SIZE / np : MAX_SET_SIZE;
        vector<int> pins(np, 0);

        for (size_t outer = 0; outer < max_outer; outer++) {
            if ((int)sets[n].size() >= MAX_SET_SIZE) break;

            for (int pi = 0; pi < np; pi++) {
                auto& P = parts[pi];
                if (outer >= P.ai.size()) continue;
                if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                if (pins[pi] >= budget) continue;

                int i = P.i, j = P.j;
                auto& [v1, d1] = P.ai[outer];
                int16_t nc1 = d1.neg_count, dp1 = d1.depth;
                int sb = (int)sets[n].size();

                for (auto& [v2, d2] : P.bj) {
                    if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                    if (pins[pi] + ((int)sets[n].size() - sb) >= budget) break;

                    int16_t nc = nc1 + d2.neg_count;
                    int16_t dp = max(dp1, d2.depth) + 1;

                    auto mk = [&](int8_t op, int8_t sa, int8_t sb, long long a, long long b, int ne) -> Deriv {
                        return {a, b, (int16_t)(nc+ne), dp, op, sa, sb};
                    };

                    // Arithmetic
                    { long long r = v1+v2; if (llabs(r) <= MAX_VAL) try_insert(n, r, mk(OP_ADD,i,j,v1,v2,0)); }
                    { long long r = v1-v2; if (llabs(r) <= MAX_VAL) try_insert(n, r, mk(OP_SUB,i,j,v1,v2,0)); }
                    if (i!=j) { long long r = v2-v1; if (llabs(r) <= MAX_VAL) try_insert(n, r, mk(OP_SUB,j,i,v2,v1,0)); }

                    if (v1 && v2 && llabs(v1) <= MAX_VAL / max(llabs(v2), 1LL))
                        try_insert(n, v1*v2, mk(OP_MUL,i,j,v1,v2,0));

                    if (v2 && v1%v2==0) try_insert(n, v1/v2, mk(OP_DIV,i,j,v1,v2,0));
                    if (i!=j && v1 && v2%v1==0) try_insert(n, v2/v1, mk(OP_DIV,j,i,v2,v1,0));

                    // Pow with sqrt-reduction
                    auto tp = [&](int8_t sa, int8_t sb, long long base, long long exp) {
                        if (base<=0 || exp<0) return;
                        if (exp<=40) { long long p=safe_pow(base,exp); if(p>0) try_insert(n,p,mk(OP_POW,sa,sb,base,exp,0)); }
                        long long rd=exp;
                        for (int k=1;k<=5;k++) {
                            if (rd%2) break; rd/=2;
                            if (rd>40||rd<1) continue;
                            long long p=safe_pow(base,rd);
                            if (p>0) try_insert(n,p,mk((int8_t)(OP_POWRT1+k-1),sa,sb,base,exp,0));
                        }
                    };
                    if (v1>0 && v2>=0 && v2<=10000) tp(i,j,v1,v2);
                    if (i!=j && v2>0 && v1>=0 && v1<=10000) tp(j,i,v2,v1);

                    // sqrt product
                    if (v1>0 && v2>0 && v1<=MAX_VAL/max(v2,1LL)) {
                        long long prod=v1*v2, sq=llround(sqrt((double)prod));
                        if (sq*sq==prod && sq<=MAX_VAL)
                            try_insert(n,sq,mk(OP_SQRTMUL,i,j,v1,v2,0));
                    }
                }
                pins[pi] += ((int)sets[n].size() - sb);
            }
        }

        apply_unary(n);
        cerr << "  S[" << n << "] = " << sets[n].size() << " values" << endl;

        if (sets[n].count(target))
            return {true, n, reconstruct(target, n)};

        // Early exit: target is 1-2 simple ops away from S[n]
        // Only check near max_digits to avoid returning suboptimal solutions
        if (n+1 <= max_digits) {
            string e = try_finish_one(n, target, digit);
            if (!e.empty()) return {true, n+1, e};
        }
        if (n+2 <= max_digits) {
            string e = try_finish_two(n, target, digit);
            if (!e.empty()) return {true, n+2, e};
        }
    }
    return {false, max_digits, ""};
}

int main(int argc, char* argv[]) {
    if (argc < 3) { cerr << "Usage: ./tchisel <digit> <target> [max]" << endl; return 1; }
    int digit = atoi(argv[1]);
    long long target = atoll(argv[2]);
    int max_digits = (argc >= 4) ? atoi(argv[3]) : 8;
    if (digit<1||digit>9) { cerr << "Error: digit 1-9" << endl; return 1; }
    if (target==0) { cerr << "Error: nonzero target" << endl; return 1; }
    if (max_digits<1||max_digits>12) { cerr << "Error: max 1-12" << endl; return 1; }

    cerr << "\n  TCHISEL v3\n  Digit:  " << digit << "\n  Target: " << target << "\n  Max:    " << max_digits << " digits\n" << endl;

    Result res = solve(digit, target, max_digits);
    if (res.found) {
        cout << "\n  SOLVED in " << res.digits << " digit(s)!" << endl;
        cout << "  " << target << " = " << res.expression << "\n" << endl;
    } else {
        cout << "\n  No solution found within " << max_digits << " digits.\n" << endl;
    }
    return 0;
}