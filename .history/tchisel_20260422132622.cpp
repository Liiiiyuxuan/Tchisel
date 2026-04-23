// ╔══════════════════════════════════════════════════════╗
// ║  TCHISEL PRETTIFIER                                  ║
// ║  Turns solver output into readable math formulas     ║
// ║  Compile: g++ -O2 -o prettify prettify.cpp           ║
// ║  Usage:   ./tchisel 2 100 | ./prettify               ║
// ║       or: echo "expression" | ./prettify              ║
// ╚══════════════════════════════════════════════════════╝

#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <cctype>
#include <algorithm>

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

    std::unique_ptr<Node> wrap_fact(std::unique_ptr<Node> n) {
        skip();
        while (peek() == '!') {
            get();
            n = Node::unary(Op::FACT, std::move(n));
        }
        return n;
    }

    bool is_op(char c) {
        return c == '+' || c == '-' || c == '*' || c == '/' || c == '^';
    }

    Op to_op(char c) {
        switch (c) {
            case '+': return Op::ADD; case '-': return Op::SUB;
            case '*': return Op::MUL; case '/': return Op::DIV;
            case '^': return Op::POW; default:  return Op::ADD;
        }
    }

public:
    Parser(const std::string& s) : s(s), pos(0) {}

    std::unique_ptr<Node> parse() {
        skip();
        auto node = parse_expr();
        return wrap_fact(std::move(node));
    }

    std::unique_ptr<Node> parse_expr() {
        skip();

        if (match("sqrt(")) {
            auto inner = parse();
            skip();
            if (peek() == ')') get();
            auto node = Node::unary(Op::SQRT, std::move(inner));
            return wrap_fact(std::move(node));
        }

        if (peek() == '-') {
            size_t saved = pos;
            get();
            skip();
            if (peek() == '(' || peek() == 's' || isdigit(peek())) {
                auto inner = parse_expr();
                auto node = Node::unary(Op::NEG, std::move(inner));
                return wrap_fact(std::move(node));
            }
            pos = saved;
        }

        if (peek() == '(') {
            get();
            auto left = parse();
            skip();

            if (is_op(peek())) {
                Op op = to_op(get());
                skip();
                auto right = parse();
                skip();
                if (peek() == ')') get();
                auto node = Node::binary(op, std::move(left), std::move(right));
                return wrap_fact(std::move(node));
            } else {
                if (peek() == ')') get();
                return wrap_fact(std::move(left));
            }
        }

        if (isdigit(peek())) {
            long long val = 0;
            while (isdigit(peek())) val = val * 10 + (get() - '0');
            auto node = Node::num(val);
            return wrap_fact(std::move(node));
        }

        return Node::num(0);
    }
};

// ─── 2D BLOCK RENDERER ─────────────────────────────────

int dwidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = s[i];
        if      (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else               i += 4;
        w++;
    }
    return w;
}

std::string spc(int n) { return std::string(std::max(0, n), ' '); }

std::string rpad(const std::string& s, int w) {
    int cur = dwidth(s);
    return cur >= w ? s : s + spc(w - cur);
}

struct Block {
    std::vector<std::string> rows;
    int baseline;

    int h() const { return (int)rows.size(); }
    int w() const { return rows.empty() ? 0 : dwidth(rows[0]); }

    void normalize() {
        int mw = 0;
        for (auto& r : rows) mw = std::max(mw, dwidth(r));
        for (auto& r : rows) r = rpad(r, mw);
    }
};

Block bpre(const std::string& s, Block b) {
    int sw = dwidth(s);
    for (int i = 0; i < b.h(); i++)
        b.rows[i] = (i == b.baseline ? s : spc(sw)) + b.rows[i];
    return b;
}

Block bapp(Block b, const std::string& s) {
    b.normalize();
    int sw = dwidth(s);
    for (int i = 0; i < b.h(); i++)
        b.rows[i] += (i == b.baseline ? s : spc(sw));
    return b;
}

Block bparens(Block b) {
    return bapp(bpre("(", std::move(b)), ")");
}

Block hcat(Block a, const std::string& sep, Block b) {
    a.normalize(); b.normalize();

    int above = std::max(a.baseline, b.baseline);
    int below = std::max(a.h() - a.baseline - 1, b.h() - b.baseline - 1);
    int total = above + 1 + below;
    int aw = a.w(), bw = b.w(), sw = dwidth(sep);

    Block r; r.baseline = above;
    for (int i = 0; i < total; i++) {
        std::string row;
        int ai = i - (above - a.baseline);
        row += (ai >= 0 && ai < a.h()) ? rpad(a.rows[ai], aw) : spc(aw);
        row += (i == above) ? sep : spc(sw);
        int bi = i - (above - b.baseline);
        row += (bi >= 0 && bi < b.h()) ? rpad(b.rows[bi], bw) : spc(bw);
        r.rows.push_back(row);
    }
    return r;
}

Block make_sqrt(Block inner) {
    inner.normalize();
    int iw = inner.w();

    Block r;
    // bar row:  space for "√" then underscores over content
    r.rows.push_back(" " + std::string(iw, '_'));

    // inner rows with √ at the inner's baseline
    for (int i = 0; i < inner.h(); i++) {
        std::string prefix = (i == inner.baseline) ? "\u221A" : " ";
        r.rows.push_back(prefix + inner.rows[i]);
    }

    r.baseline = inner.baseline + 1;
    return r;
}

// ─── RENDER ─────────────────────────────────────────────

int prec(Op op) {
    switch (op) {
        case Op::ADD: case Op::SUB: return 1;
        case Op::MUL: case Op::DIV: return 2;
        case Op::POW: return 3;
        default: return 10;
    }
}

bool is_bin(Op op) {
    return op == Op::ADD || op == Op::SUB ||
           op == Op::MUL || op == Op::DIV || op == Op::POW;
}

Block render(const Node* n) {
    if (!n) return {{"?"}, 0};

    switch (n->op) {
        case Op::NUM:
            return {{std::to_string(n->val)}, 0};

        case Op::NEG: {
            Block inner = render(n->left.get());
            if (n->left->op == Op::NUM || n->left->op == Op::FACT ||
                n->left->op == Op::SQRT) {
                return bpre("-", std::move(inner));
            }
            return bpre("-", bparens(std::move(inner)));
        }

        case Op::SQRT:
            return make_sqrt(render(n->left.get()));

        case Op::FACT: {
            Block inner = render(n->left.get());
            if (n->left->op == Op::NUM || n->left->op == Op::FACT) {
                return bapp(std::move(inner), "!");
            }
            return bapp(bparens(std::move(inner)), "!");
        }

        case Op::ADD: case Op::SUB:
        case Op::MUL: case Op::DIV:
        case Op::POW: {
            int mp = prec(n->op);

            Block l = render(n->left.get());
            Block r = render(n->right.get());

            bool wl = is_bin(n->left->op) && prec(n->left->op) < mp;
            bool wr = is_bin(n->right->op) &&
                      (prec(n->right->op) < mp ||
                       (prec(n->right->op) == mp &&
                        (n->op == Op::SUB || n->op == Op::DIV || n->op == Op::POW)));

            if (wl) l = bparens(std::move(l));
            if (wr) r = bparens(std::move(r));

            std::string op_str;
            switch (n->op) {
                case Op::ADD: op_str = " + "; break;
                case Op::SUB: op_str = " - "; break;
                case Op::MUL: op_str = " \u00D7 "; break;
                case Op::DIV: op_str = " / "; break;
                case Op::POW: op_str = "^";   break;
                default: break;
            }

            return hcat(std::move(l), op_str, std::move(r));
        }

        default:
            return {{"?"}, 0};
    }
}

// ─── MAIN ───────────────────────────────────────────────

std::string extract_expr(const std::string& line) {
    auto eq = line.find("= ");
    if (eq != std::string::npos) return line.substr(eq + 2);
    size_t start = line.find_first_not_of(" \t");
    return start == std::string::npos ? "" : line.substr(start);
}

std::string extract_prefix(const std::string& line) {
    auto eq = line.find("= ");
    return eq != std::string::npos ? line.substr(0, eq + 2) : "";
}

int main() {
    std::string line;

    while (std::getline(std::cin, line)) {
        if (line.empty()) { std::cout << "\n"; continue; }

        std::string expr = extract_expr(line);
        if (expr.empty()) { std::cout << line << "\n"; continue; }

        char first = ' ';
        for (char c : expr) if (c != ' ') { first = c; break; }
        if (first != '(' && first != '-' && first != 's' && !isdigit(first)) {
            std::cout << line << "\n";
            continue;
        }

        std::string prefix = extract_prefix(line);

        Parser parser(expr);
        auto tree = parser.parse();
        Block blk = render(tree.get());

        int pw = prefix.size();
        for (int i = 0; i < blk.h(); i++) {
            if (i == blk.baseline && !prefix.empty()) {
                std::cout << "  " << prefix << blk.rows[i] << "\n";
            } else {
                std::cout << "  " << spc(pw) << blk.rows[i] << "\n";
            }
        }
        std::cout << "\n";
    }

    return 0;
}