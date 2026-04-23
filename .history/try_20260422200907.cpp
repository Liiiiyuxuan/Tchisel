#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

struct Node {
    bool isLeaf;
    int value;
    shared_ptr<Node> left;
    shared_ptr<Node> right;

    explicit Node(int v) : isLeaf(true), value(v) {}
    Node(shared_ptr<Node> l, shared_ptr<Node> r)
        : isLeaf(false), value(0), left(std::move(l)), right(std::move(r)) {}
};

struct ExprVal {
    double value;
    string expr;
};

class Solver {
private:
    char digit_;
    static constexpr double EPS = 1e-5;
    static constexpr double MAX_ABS = 1e12;

    unordered_map<string, vector<shared_ptr<Node>>> treeMemo_;
    unordered_map<const Node*, vector<ExprVal>> variantMemo_;

    static bool finiteReasonable(double x) {
        return std::isfinite(x) && std::fabs(x) <= MAX_ABS;
    }

    static bool approxInteger(double x) {
        return std::fabs(x - std::round(x)) < 1e-9;
    }

    static double rounded4(double x) {
        return std::round(x * 10000.0) / 10000.0;
    }

    static bool matches(double x, double target) {
        return std::fabs(rounded4(x) - target) < EPS;
    }

    void addExpr(vector<ExprVal>& out,
                 unordered_set<string>& seen,
                 double value,
                 const string& expr) {
        if (!finiteReasonable(value)) return;
        if (seen.insert(expr).second) {
            out.push_back({value, expr});
        }
    }

    void addUnaryVariants(vector<ExprVal>& out,
                          unordered_set<string>& seen,
                          double value,
                          const string& baseExpr) {
        addExpr(out, seen, value, baseExpr);
        addExpr(out, seen, -value, "-(" + baseExpr + ")");

        if (value >= -1e-12) {
            double clipped = (value < 0.0 ? 0.0 : value);
            addExpr(out, seen, std::sqrt(clipped), "sqrt(" + baseExpr + ")");
        }
    }

    vector<shared_ptr<Node>> getTrees(const string& s) {
        auto it = treeMemo_.find(s);
        if (it != treeMemo_.end()) return it->second;

        vector<shared_ptr<Node>> trees;

        trees.push_back(make_shared<Node>(stoi(s)));

        for (int i = 1; i < (int)s.size(); ++i) {
            string leftStr = s.substr(0, i);
            string rightStr = s.substr(i);

            auto leftTrees = getTrees(leftStr);
            auto rightTrees = getTrees(rightStr);

            for (const auto& l : leftTrees) {
                for (const auto& r : rightTrees) {
                    trees.push_back(make_shared<Node>(l, r));
                }
            }
        }

        treeMemo_[s] = trees;
        return trees;
    }

    vector<ExprVal> getVariants(const shared_ptr<Node>& node) {
        auto it = variantMemo_.find(node.get());
        if (it != variantMemo_.end()) return it->second;

        vector<ExprVal> result;
        unordered_set<string> seen;

        if (node->isLeaf) {
            string s = to_string(node->value);
            addUnaryVariants(result, seen, (double)node->value, s);
        } else {
            auto leftVars = getVariants(node->left);
            auto rightVars = getVariants(node->right);

            for (const auto& L : leftVars) {
                for (const auto& R : rightVars) {
                    addUnaryVariants(result, seen, L.value + R.value,
                                     "(" + L.expr + "+" + R.expr + ")");

                    addUnaryVariants(result, seen, L.value - R.value,
                                     "(" + L.expr + "-" + R.expr + ")");

                    addUnaryVariants(result, seen, L.value * R.value,
                                     "(" + L.expr + "*" + R.expr + ")");

                    if (std::fabs(R.value) > 1e-12) {
                        addUnaryVariants(result, seen, L.value / R.value,
                                         "(" + L.expr + "/" + R.expr + ")");
                    }

                    bool okPow = false;
                    double powValue = 0.0;

                    if (L.value >= 0.0) {
                        powValue = std::pow(L.value, R.value);
                        okPow = true;
                    } else if (approxInteger(R.value)) {
                        powValue = std::pow(L.value, std::round(R.value));
                        okPow = true;
                    }

                    if (okPow && finiteReasonable(powValue)) {
                        addUnaryVariants(result, seen, powValue,
                                         "pow(" + L.expr + "," + R.expr + ")");
                    }
                }
            }
        }

        variantMemo_[node.get()] = result;
        return result;
    }

public:
    explicit Solver(char digit) : digit_(digit) {}

    vector<string> solve(int count, double target) {
        treeMemo_.clear();
        variantMemo_.clear();

        string digits(count, digit_);
        auto trees = getTrees(digits);

        vector<string> answers;
        unordered_set<string> seen;

        for (const auto& tree : trees) {
            auto vars = getVariants(tree);
            for (const auto& ev : vars) {
                if (matches(ev.value, target) && seen.insert(ev.expr).second) {
                    answers.push_back(ev.expr);
                }
            }
        }

        sort(answers.begin(), answers.end(),
             [](const string& a, const string& b) {
                 if (a.size() != b.size()) return a.size() < b.size();
                 return a < b;
             });

        return answers;
    }
};

int main(int argc, char** argv) {
    if (argc != 4) {
        cerr << "Usage: " << argv[0] << " <count> <digit 1-9> <target>\n";
        cerr << "Example: " << argv[0] << " 3 8 1024\n";
        return 1;
    }

    int count = std::atoi(argv[1]);
    int digit = std::atoi(argv[2]);
    double target = std::atof(argv[3]);

    if (count <= 0) {
        cerr << "count must be positive.\n";
        return 1;
    }
    if (digit < 1 || digit > 9) {
        cerr << "digit must be between 1 and 9.\n";
        return 1;
    }

    Solver solver((char)('0' + digit));
    auto answers = solver.solve(count, target);

    if (answers.empty()) {
        cout << "No solution found.\n";
        return 0;
    }

    cout << "Found " << answers.size() << " solution(s):\n";
    for (const auto& s : answers) {
        cout << s << '\n';
    }

    return 0;
}