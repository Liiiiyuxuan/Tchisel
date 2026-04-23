// ╔══════════════════════════════════════════════════╗
// ║  TCHISEL — Tchisla Solver                        ║
// ║  Express any number using a single digit          ║
// ║  Compile: g++ -O2 -o tchisel tchisel.cpp          ║
// ║  Usage:   ./tchisel <digit> <target> [max_digits] ║
// ╚══════════════════════════════════════════════════╝

#include <iostream>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <string>
#include <cstdlib>
#include <climits>
#include <algorithm>

using namespace std;

const long long MAX_VAL = 1000000000000LL; // 1e12
const int MAX_SET_SIZE = 50000;

// ─── Expression quality ─────────────────────────────────
// Lower score = cleaner expression. We penalize negations
// heavily so the solver prefers positive-form expressions.

int expr_score(const string& e) {
    int negs = 0;
    int len = (int)e.size();
    for (size_t i = 0; i + 1 < e.size(); i++) {
        if (e[i] == '-' && e[i + 1] == '(') negs++;
    }
    return negs * 1000 + len;
}

// ─── Storage ────────────────────────────────────────────

struct ValEntry {
    string expr;
    int score;
};

unordered_map<long long, ValEntry> sets[10];

long long factorial_table[21];

void precompute_factorials() {
    factorial_table[0] = 1;
    factorial_table[1] = 1;
    for (int i = 2; i <= 20; i++) {
        factorial_table[i] = factorial_table[i - 1] * i;
        if (factorial_table[i] > MAX_VAL) {
            factorial_table[i] = MAX_VAL + 1;
        }
    }
}

// Insert or replace if better expression found
bool try_insert(int n, long long val, const string& expr) {
    if (val > MAX_VAL || val < -MAX_VAL) return false;

    int sc = expr_score(expr);

    auto it = sets[n].find(val);
    if (it != sets[n].end()) {
        // Replace if new expression is cleaner
        if (sc < it->second.score) {
            it->second = {expr, sc};
            return true;
        }
        return false;
    }

    if ((int)sets[n].size() >= MAX_SET_SIZE) return false;
    sets[n][val] = {expr, sc};
    return true;
}

// ─── Unary operations ───────────────────────────────────

void apply_unary(int n) {
    vector<pair<long long, string>> entries;
    entries.reserve(sets[n].size());
    for (auto& [val, ve] : sets[n]) {
        entries.push_back({val, ve.expr});
    }

    auto do_unary = [&](const vector<pair<long long, string>>& src) {
        vector<pair<long long, string>> added;
        for (auto& [val, expr] : src) {
            // sqrt — only if perfect square
            if (val > 0) {
                long long sq = (long long)round(sqrt((double)val));
                if (sq * sq == val && sq > 0 && sq < MAX_VAL) {
                    string e = "sqrt(" + expr + ")";
                    if (try_insert(n, sq, e))
                        added.push_back({sq, e});
                    // double sqrt
                    long long sq2 = (long long)round(sqrt((double)sq));
                    if (sq2 * sq2 == sq && sq2 > 0 && sq2 < MAX_VAL) {
                        string e2 = "sqrt(sqrt(" + expr + "))";
                        if (try_insert(n, sq2, e2))
                            added.push_back({sq2, e2});
                    }
                }
            }

            // factorial — only small positive integers (3..12)
            if (val >= 3 && val <= 12) {
                long long f = factorial_table[val];
                if (f <= MAX_VAL) {
                    string e = "(" + expr + ")!";
                    if (try_insert(n, f, e))
                        added.push_back({f, e});
                }
            }

            // negation — only negate positive values
            if (val > 0) {
                string e = "-(" + expr + ")";
                if (try_insert(n, -val, e))
                    added.push_back({-val, e});
            }
        }
        return added;
    };

    // Two passes to catch chains like factorial(sqrt(...))
    auto added1 = do_unary(entries);
    do_unary(added1);
}

// ─── Safe exponentiation ────────────────────────────────

long long safe_pow(long long base, long long exp) {
    if (exp < 0) {
        if (base == 1) return 1;
        if (base == -1) return (exp % 2 == 0) ? 1 : -1;
        return 0;
    }
    if (exp == 0) return 1;
    if (base == 0) return 0;
    if (base == 1) return 1;
    if (base == -1) return (exp % 2 == 0) ? 1 : -1;
    if (abs(base) > 1000 || exp > 40) return 0;

    long long result = 1;
    long long b = abs(base);
    for (long long i = 0; i < exp; i++) {
        result *= b;
        if (result > MAX_VAL) return 0;
    }
    if (base < 0 && exp % 2 == 1) result = -result;
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
        // Concatenation: d, dd, ddd, ...
        long long concat = 0;
        for (int i = 0; i < n; i++) concat = concat * 10 + digit;
        try_insert(n, concat, to_string(concat));

        // Combine sets[i] x sets[j] where i + j = n
        for (int i = 1; i <= n / 2; i++) {
            int j = n - i;
            if (sets[i].empty() || sets[j].empty()) continue;

            for (auto& [v1, ve1] : sets[i]) {
                if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                const string& e1 = ve1.expr;

                for (auto& [v2, ve2] : sets[j]) {
                    if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                    const string& e2 = ve2.expr;

                    // Addition
                    {
                        long long r = v1 + v2;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, "(" + e1 + " + " + e2 + ")");
                    }

                    // Subtraction (both ways)
                    {
                        long long r = v1 - v2;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, "(" + e1 + " - " + e2 + ")");
                    }
                    if (i != j) {
                        long long r = v2 - v1;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, "(" + e2 + " - " + e1 + ")");
                    }

                    // Multiplication
                    if (v1 != 0 && v2 != 0 && abs(v1) <= MAX_VAL / max(abs(v2), 1LL)) {
                        long long r = v1 * v2;
                        if (abs(r) <= MAX_VAL)
                            try_insert(n, r, "(" + e1 + " * " + e2 + ")");
                    }

                    // Division (both ways, only if exact)
                    if (v2 != 0 && v1 % v2 == 0)
                        try_insert(n, v1 / v2, "(" + e1 + " / " + e2 + ")");
                    if (i != j && v1 != 0 && v2 % v1 == 0)
                        try_insert(n, v2 / v1, "(" + e2 + " / " + e1 + ")");

                    // Exponentiation (both ways, positive bases only)
                    // (-a)^n = a^n for even n, and negation handles odd n,
                    // so skipping negative bases loses nothing but avoids
                    // misleading expressions like sqrt((-6)^6).
                    if (v2 >= -20 && v2 <= 40 && v1 > 0) {
                        long long p = safe_pow(v1, v2);
                        if (p != 0)
                            try_insert(n, p, "(" + e1 + " ^ " + e2 + ")");
                    }
                    if (i != j && v1 >= -20 && v1 <= 40 && v2 > 0) {
                        long long p = safe_pow(v2, v1);
                        if (p != 0)
                            try_insert(n, p, "(" + e2 + " ^ " + e1 + ")");
                    }

                    // sqrt of product (if perfect square)
                    if (v1 > 0 && v2 > 0 && v1 <= MAX_VAL / max(v2, 1LL)) {
                        long long prod = v1 * v2;
                        long long sq = (long long)round(sqrt((double)prod));
                        if (sq * sq == prod && sq <= MAX_VAL)
                            try_insert(n, sq, "sqrt(" + e1 + " * " + e2 + ")");
                    }
                }
            }
        }

        // Apply unary ops
        apply_unary(n);

        cerr << "  S[" << n << "] = " << sets[n].size() << " values" << endl;

        if (sets[n].count(target)) {
            return {true, n, sets[n][target].expr};
        }
    }

    return {false, max_digits, ""};
}

// ─── Main ───────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "╔════════════════════════════════════════╗" << endl;
        cerr << "║         TCHISEL — Tchisla Solver       ║" << endl;
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
    cerr << "  TCHISEL" << endl;
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