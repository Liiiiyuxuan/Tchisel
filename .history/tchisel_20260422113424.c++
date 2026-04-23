#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ExprVal {
    double value;
    std::string expr;
};

class TchislaSolver {
public:
    TchislaSolver(int digit, double target)
        : digit_(digit), target_(target) {}

    std::vector<std::string> solve(int count) {
        memo_.clear();
        auto all = generate(count);

        std::vector<std::string> answers;
        std::unordered_set<std::string> seen;

        for (const auto& ev : all) {
            if (matchesTarget(ev.value) && seen.insert(ev.expr).second) {
                answers.push_back(ev.expr);
            }
        }

        std::sort(
            answers.begin(), answers.end(),
            [](const std::string& a, const std::string& b) {
                if (a.size() != b.size()) return a.size() < b.size();
                return a < b;
            });

        return answers;
    }

private:
    static constexpr double EPS = 1e-5;
    static constexpr double MAX_ABS = 1e12;

    int digit_;
    double target_;
    std::unordered_map<int, std::vector<ExprVal>> memo_;

    static bool isFiniteReasonable(double x) {
        return std::isfinite(x) && std::fabs(x) <= MAX_ABS;
    }

    static bool approxInteger(double x) {
        return std::fabs(x - std::round(x)) < 1e-9;
    }

    bool matchesTarget(double x) const {
        double rounded = std::round(x * 10000.0) / 10000.0;
        return std::fabs(rounded - target_) < EPS;
    }

    int repeatedNumber(int count) const {
        int value = 0;
        for (int i = 0; i < count; ++i) {
            value = value * 10 + digit_;
        }
        return value;
    }

    void addIfValid(std::vector<ExprVal>& out,
                    std::unordered_set<std::string>& seen,
                    double value,
                    const std::string& expr) const {
        if (!isFiniteReasonable(value)) return;
        if (seen.insert(expr).second) {
            out.push_back({value, expr});
        }
    }

    void addUnaryVariants(  std::vector<ExprVal>& out,
                            std::unordered_set<std::string>& seen,
                            double value,
                            const std::string& expr) const {
        addIfValid(out, seen, value, expr);

        addIfValid(out, seen, -value, "-(" + expr + ")");

        if (value >= -1e-12) {
            double clipped = (value < 0.0 ? 0.0 : value);
            addIfValid(out, seen, std::sqrt(clipped), "sqrt(" + expr + ")");
        }
    }

    std::vector<ExprVal> generate(int count) {
        auto it = memo_.find(count);
        if (it != memo_.end()) return it->second;

        std::vector<ExprVal> result;
        std::unordered_set<std::string> seen;

        int concatVal = repeatedNumber(count);
        addUnaryVariants(   
            result, seen,
            static_cast<double>(concatVal),
            std::to_string(concatVal)
        );

        for (int leftCount = 1; leftCount < count; ++leftCount) {
            int rightCount = count - leftCount;
            auto lefts = generate(leftCount);
            auto rights = generate(rightCount);

            for (const auto& L : lefts) {
                for (const auto& R : rights) {
                    addUnaryVariants(
                        result, seen,
                        L.value + R.value,
                        "(" + L.expr + "+" + R.expr + ")"
                    );

                    addUnaryVariants(result, seen,
                                     L.value - R.value,
                                     "(" + L.expr + "-" + R.expr + ")");

                    addUnaryVariants(result, seen,
                                     L.value * R.value,
                                     "(" + L.expr + "*" + R.expr + ")");

                    if (std::fabs(R.value) > 1e-12) {
                        addUnaryVariants(result, seen,
                                         L.value / R.value,
                                         "(" + L.expr + "/" + R.expr + ")");
                    }

                    bool okPow = false;
                    double powValue = std::numeric_limits<double>::quiet_NaN();

                    if (L.value >= 0.0) {
                        powValue = std::pow(L.value, R.value);
                        okPow = true;
                    } else if (approxInteger(R.value)) {
                        powValue = std::pow(L.value, std::round(R.value));
                        okPow = true;
                    }

                    if (okPow) {
                        addUnaryVariants(result, seen,
                                         powValue,
                                         "pow(" + L.expr + "," + R.expr + ")");
                    }
                }
            }
        }

        memo_[count] = result;
        return result;
    }
};

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <digit 1-9> <count> <target>\n";
        std::cerr << "Example: " << argv[0] << " 8 3 1024\n";
        return 1;
    }

    int digit = std::atoi(argv[1]);
    int count = std::atoi(argv[2]);
    double target = std::atof(argv[3]);

    if (digit < 1 || digit > 9 || count <= 0) {
        std::cerr << "digit must be in [1,9] and count must be positive.\n";
        return 1;
    }

    TchislaSolver solver(digit, target);
    auto answers = solver.solve(count);

    if (answers.empty()) {
        std::cout << "No solution found.\n";
        return 0;
    }

    std::cout << "Found " << answers.size() << " solution(s):\n";
    for (const auto& s : answers) {
        std::cout << s << '\n';
    }

    return 0;
}