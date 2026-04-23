// ╔══════════════════════════════════════════════════════╗
// ║  TCHISEL PRETTIFIER                                  ║
// ║  Turns solver output into readable math formulas     ║
// ║  Compile: g++ -O2 -o prettify prettify.cpp           ║
// ║  Usage:   ./tchisel 2 100 | ./prettify               ║
// ║       or: echo "expression" | ./prettify              ║
// ║       or: ./prettify   (then type expressions)        ║
// ╚══════════════════════════════════════════════════════╝

#include <iostream>
#include <string>
#include <memory>
#include <cctype>
#include <sstream>

// ─── AST ────────────────────────────────────────────────

enum class Op { NUM, NEG, SQRT, FACT, ADD, SUB, MUL, DIV, POW };

struct Node {
    Op op;
    long long val;
    std::unique_ptr<Node> left, right;

    static std::unique_ptr<Node> num(long long v) {
        auto n = std::make_unique<Node>();
        n->op = Op::NUM; n->val = v;
        return n;
    }
    static std::unique_ptr<Node> unary(Op o, std::unique_ptr<Node> child) {
        auto n = std::make_unique<Node>();
        n->op = o; n->left = std::move(child);
        return n;
    }
    static std::unique_ptr<Node> binary(Op o, std::unique_ptr<Node> l, std::unique_ptr<Node> r) {
        auto n = std::make_unique<Node>();
        n->op = o; n->left = std::move(l); n->right = std::move(r);
        return n;
    }
};

// ─── PARSER ─────────────────────────────────────────────
// Grammar of solver output:
//   expr = "sqrt(" expr ")"          → sqrt
//        | "-(" expr ")"             → negation (only when - precedes '(')
//        | "(" expr OP expr ")"      → binary op
//        | "(" expr ")"              → grouping
//        | NUMBER                    → literal
//   After any expr, trailing "!" means factorial (can chain)

class Parser {
    const std::string& s;
    size_t pos;

    char peek() { return pos < s.size() ? s[pos] : '\0'; }
    char get()  { return s[pos++]; }
    void skip() { while (pos < s.size() && s[pos] == ' ') pos++; }

    bool match(const std::string& word) {
        if (s.compare(pos, word.size(), word) == 0) {
            pos += word.size();
            return true;
        }
        return false;
    }

    // Wrap node in factorial nodes for each trailing '!'
    std::unique_ptr<Node> wrap_factorials(std::unique_ptr<Node> n) {
        skip();
        while (peek() == '!') {
            get();
            n = Node::unary(Op::FACT, std::move(n));
        }
        return n;
    }

    Op char_to_op(char c) {
        switch (c) {
            case '+': return Op::ADD;
            case '-': return Op::SUB;
            case '*': return Op::MUL;
            case '/': return Op::DIV;
            case '^': return Op::POW;
            default:  return Op::ADD;
        }
    }

    bool is_op_char(char c) {
        return c == '+' || c == '-' || c == '*' || c == '/' || c == '^';
    }

public:
    Parser(const std::string& s) : s(s), pos(0) {}

    std::unique_ptr<Node> parse() {
        skip();
        auto node = parse_expr();
        return wrap_factorials(std::move(node));
    }

    std::unique_ptr<Node> parse_expr() {
        skip();

        // sqrt(...)
        if (match("sqrt(")) {
            auto inner = parse();
            skip();
            if (peek() == ')') get();
            auto node = Node::unary(Op::SQRT, std::move(inner));
            return wrap_factorials(std::move(node));
        }

        // Negation: - followed by ( or sqrt or digit
        if (peek() == '-') {
            size_t saved = pos;
            get(); // consume -
            skip();
            if (peek() == '(' || peek() == 's' || isdigit(peek())) {
                auto inner = parse_expr();
                auto node = Node::unary(Op::NEG, std::move(inner));
                return wrap_factorials(std::move(node));
            }
            pos = saved; // backtrack
        }

        // Parenthesized: (expr) or (expr OP expr)
        if (peek() == '(') {
            get(); // consume (
            auto left = parse();
            skip();

            if (is_op_char(peek())) {
                Op op = char_to_op(get());
                skip();
                auto right = parse();
                skip();
                if (peek() == ')') get();
                auto node = Node::binary(op, std::move(left), std::move(right));
                return wrap_factorials(std::move(node));
            } else {
                // just grouping (expr)
                if (peek() == ')') get();
                return wrap_factorials(std::move(left));
            }
        }

        // Number
        if (isdigit(peek())) {
            long long val = 0;
            while (isdigit(peek())) {
                val = val * 10 + (get() - '0');
            }
            auto node = Node::num(val);
            return wrap_factorials(std::move(node));
        }

        // Fallback
        return Node::num(0);
    }
};

// ─── PRETTY PRINTER ─────────────────────────────────────

int prec(Op op) {
    switch (op) {
        case Op::ADD: case Op::SUB: return 1;
        case Op::MUL: case Op::DIV: return 2;
        case Op::POW: return 3;
        default: return 10;
    }
}

bool is_binary(Op op) {
    return op == Op::ADD || op == Op::SUB ||
           op == Op::MUL || op == Op::DIV || op == Op::POW;
}

// Check if a node is "atomic" (no parens needed around it in most contexts)
bool is_atomic(const Node* n) {
    return n->op == Op::NUM || n->op == Op::FACT || n->op == Op::SQRT;
}

std::string pretty(const Node* n) {
    if (!n) return "?";

    switch (n->op) {
        case Op::NUM:
            return std::to_string(n->val);

        case Op::NEG: {
            std::string inner = pretty(n->left.get());
            // If child is a plain number, just prepend minus
            if (n->left->op == Op::NUM) {
                return "-" + inner;
            }
            // If child is atomic (sqrt, factorial), no extra parens
            if (is_atomic(n->left.get())) {
                return "-" + inner;
            }
            // Otherwise wrap
            return "-(" + inner + ")";
        }

        case Op::SQRT: {
            std::string inner = pretty(n->left.get());
            return "\u221A(" + inner + ")";
        }

        case Op::FACT: {
            std::string inner = pretty(n->left.get());
            // If child is a number or another factorial, no parens needed
            if (n->left->op == Op::NUM || n->left->op == Op::FACT) {
                return inner + "!";
            }
            // If child is negation of a number: (-2)!
            if (n->left->op == Op::NEG && n->left->left->op == Op::NUM) {
                return "(" + inner + ")!";
            }
            return "(" + inner + ")!";
        }

        case Op::ADD: case Op::SUB:
        case Op::MUL: case Op::DIV:
        case Op::POW: {
            int my_prec = prec(n->op);

            std::string l = pretty(n->left.get());
            std::string r = pretty(n->right.get());

            // Determine if children need wrapping
            bool wrap_l = is_binary(n->left->op) && prec(n->left->op) < my_prec;
            bool wrap_r = is_binary(n->right->op) &&
                          (prec(n->right->op) < my_prec ||
                           (prec(n->right->op) == my_prec &&
                            (n->op == Op::SUB || n->op == Op::DIV || n->op == Op::POW)));

            if (wrap_l) l = "(" + l + ")";
            if (wrap_r) r = "(" + r + ")";

            // Operator symbol
            std::string op_str;
            switch (n->op) {
                case Op::ADD: op_str = " + "; break;
                case Op::SUB: op_str = " - "; break;
                case Op::MUL: op_str = " \u00D7 "; break;
                case Op::DIV: op_str = " / "; break;
                case Op::POW: op_str = "^";   break;
                default: break;
            }

            return l + op_str + r;
        }

        default:
            return "?";
    }
}

// ─── MAIN ───────────────────────────────────────────────

std::string extract_expression(const std::string& line) {
    // If line contains "= ", extract everything after it
    auto eq = line.find("= ");
    if (eq != std::string::npos) {
        return line.substr(eq + 2);
    }
    // Otherwise treat whole line as expression
    // Skip leading whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    return line.substr(start);
}

std::string extract_prefix(const std::string& line) {
    auto eq = line.find("= ");
    if (eq != std::string::npos) {
        return line.substr(0, eq + 2);
    }
    return "";
}

int main() {
    std::string line;

    std::cerr << "  TCHISEL PRETTIFIER" << std::endl;
    std::cerr << "  Paste expressions (or pipe from solver)" << std::endl;
    std::cerr << std::endl;

    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::string expr_str = extract_expression(line);
        if (expr_str.empty()) {
            // Pass through non-expression lines
            std::cout << line << std::endl;
            continue;
        }

        // Try to parse — if it doesn't start with valid tokens, pass through
        char first = ' ';
        for (char c : expr_str) {
            if (c != ' ') { first = c; break; }
        }
        if (first != '(' && first != '-' && first != 's' && !isdigit(first)) {
            std::cout << line << std::endl;
            continue;
        }

        std::string prefix = extract_prefix(line);

        Parser parser(expr_str);
        auto tree = parser.parse();
        std::string result = pretty(tree.get());

        if (!prefix.empty()) {
            std::cout << "  " << prefix << result << std::endl;
        } else {
            std::cout << "  " << result << std::endl;
        }
    }

    return 0;
}