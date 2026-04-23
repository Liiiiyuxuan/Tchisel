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

const double EPSILON = 1e-9;
const long long MAX_VAL = 1000000000000LL; // 1e12
const int MAX_SET_SIZE = 150000;

// Each reachable value is stored with its expression string
struct Entry {
    long long val;
    string expr;
};

// sets[n] = all values reachable with exactly n copies of the digit
// Using unordered_map: value -> expression
unordered_map<long long, string> sets[10]; // max 9 digits

long long factorial_table[21];

void precompute_factorials() {
    factorial_table[0] = 1;
    factorial_table[1] = 1;
    for (int i = 2; i <= 20; i++) {
        factorial_table[i] = factorial_table[i - 1] * i;
        if (factorial_table[i] > MAX_VAL) {
            factorial_table[i] = MAX_VAL + 1; // mark overflow
        }
    }
}

// Try to insert a value into sets[n], respecting size limit
bool try_insert(int n, long long val, const string& expr) {
    if (val > MAX_VAL || val < -MAX_VAL) return false;
    if ((int)sets[n].size() >= MAX_SET_SIZE) return false;
    if (sets[n].count(val)) return false;
    sets[n][val] = expr;
    return true;
}

// Apply unary operations (sqrt, factorial, negation) to expand sets[n]
void apply_unary(int n) {
    // Collect current entries to avoid modifying while iterating
    vector<pair<long long, string>> entries(sets[n].begin(), sets[n].end());

    auto do_unary = [&](const vector<pair<long long, string>>& src) {
        vector<pair<long long, string>> added;
        for (auto& [val, expr] : src) {
            // sqrt — only if perfect square
            if (val > 0) {
                long long sq = (long long)round(sqrt((double)val));
                if (sq * sq == val && sq > 0 && sq < MAX_VAL) {
                    if (!sets[n].count(sq)) {
                        string e = "sqrt(" + expr + ")";
                        if (try_insert(n, sq, e))
                            added.push_back({sq, e});
                    }
                    // double sqrt
                    long long sq2 = (long long)round(sqrt((double)sq));
                    if (sq2 * sq2 == sq && sq2 > 0 && sq2 < MAX_VAL) {
                        if (!sets[n].count(sq2)) {
                            string e2 = "sqrt(sqrt(" + expr + "))";
                            if (try_insert(n, sq2, e2))
                                added.push_back({sq2, e2});
                        }
                    }
                }
            }

            // factorial — only small positive integers (3..12)
            if (val >= 3 && val <= 12) {
                long long f = factorial_table[val];
                if (f <= MAX_VAL && !sets[n].count(f)) {
                    string e = "(" + expr + ")!";
                    if (try_insert(n, f, e))
                        added.push_back({f, e});
                }
            }

            // negation
            if (val > 0 && !sets[n].count(-val)) {
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

// Safe exponentiation: returns 0 if overflow or invalid
long long safe_pow(long long base, long long exp) {
    if (exp < 0) {
        // Only handle if result is integer (i.e., base is ±1)
        if (base == 1) return 1;
        if (base == -1) return (exp % 2 == 0) ? 1 : -1;
        return 0; // not integer
    }
    if (exp == 0) return 1;
    if (base == 0) return 0;
    if (base == 1) return 1;
    if (base == -1) return (exp % 2 == 0) ? 1 : -1;

    // Prevent huge computations
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

struct Result {
    bool found;
    int digits;
    string expression;
};

Result solve(int digit, long long target, int max_digits) {
    precompute_factorials();

    // Clear sets
    for (int i = 0; i <= 9; i++) sets[i].clear();

    for (int n = 1; n <= max_digits; n++) {
        // Concatenation: d, dd, ddd, ...
        long long concat = 0;
        for (int i = 0; i < n; i++) {
            concat = concat * 10 + digit;
        }
        string concat_str = to_string(concat);
        try_insert(n, concat, concat_str);

        // Combine sets[i] x sets[j] where i + j = n
        for (int i = 1; i <= n / 2; i++) {
            int j = n - i;
            if (sets[i].empty() || sets[j].empty()) continue;

            for (auto& [v1, e1] : sets[i]) {
                if ((int)sets[n].size() >= MAX_SET_SIZE) break;
                for (auto& [v2, e2] : sets[j]) {
                    if ((int)sets[n].size() >= MAX_SET_SIZE) break;

                    // When i != j, we try both orderings.
                    // When i == j, (v1,v2) and (v2,v1) are both iterated,
                    // so commutative ops are covered.

                    // Addition
                    long long sum = v1 + v2;
                    if (abs(sum) <= MAX_VAL)
                        try_insert(n, sum, "(" + e1 + " + " + e2 + ")");

                    // Subtraction (both ways)
                    long long diff1 = v1 - v2;
                    if (abs(diff1) <= MAX_VAL)
                        try_insert(n, diff1, "(" + e1 + " - " + e2 + ")");
                    if (i != j) {
                        long long diff2 = v2 - v1;
                        if (abs(diff2) <= MAX_VAL)
                            try_insert(n, diff2, "(" + e2 + " - " + e1 + ")");
                    }

                    // Multiplication
                    if (v1 != 0 && v2 != 0 && abs(v1) <= MAX_VAL / max(abs(v2), 1LL)) {
                        long long prod = v1 * v2;
                        if (abs(prod) <= MAX_VAL)
                            try_insert(n, prod, "(" + e1 + " * " + e2 + ")");
                    }

                    // Division (both ways, only if exact)
                    if (v2 != 0 && v1 % v2 == 0) {
                        try_insert(n, v1 / v2, "(" + e1 + " / " + e2 + ")");
                    }
                    if (i != j && v1 != 0 && v2 % v1 == 0) {
                        try_insert(n, v2 / v1, "(" + e2 + " / " + e1 + ")");
                    }

                    // Exponentiation (both ways)
                    if (v2 >= -20 && v2 <= 40 && v1 != 0) {
                        long long p = safe_pow(v1, v2);
                        if (p != 0 || (v1 == 0 && v2 > 0))
                            try_insert(n, p, "(" + e1 + " ^ " + e2 + ")");
                    }
                    if (i != j && v1 >= -20 && v1 <= 40 && v2 != 0) {
                        long long p = safe_pow(v2, v1);
                        if (p != 0 || (v2 == 0 && v1 > 0))
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

        // Progress
        cerr << "  S[" << n << "] = " << sets[n].size() << " values" << endl;

        // Check for target
        if (sets[n].count(target)) {
            return {true, n, sets[n][target]};
        }
    }

    return {false, max_digits, ""};
}

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