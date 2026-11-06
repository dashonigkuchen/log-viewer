#include "core/search/bool_expr.h"

#include "core/search/literal_term.h"
#include "core/search/regex_matcher.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace lv::core
{

namespace
{

enum class TokenKind : std::uint8_t
{
    kLParen,
    kRParen,
    kAnd,
    kOr,
    kNot,
    kLiteral,
    kRegex,
    kEnd,
};

struct Token
{
    TokenKind kind = TokenKind::kEnd;
    std::size_t offset = 0;  // byte offset in the expression
    std::string text;        // literal text or regex pattern (unescaped)
    std::string source;      // the token as written
    bool caseless = false;   // regex flag `i`
};

Token makeToken(TokenKind kind, std::size_t offset)
{
    Token token;
    token.kind = kind;
    token.offset = offset;
    return token;
}

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isWordChar(char c) noexcept
{
    const auto byte = static_cast<unsigned char>(c);
    return !isSpace(c) && c != '(' && c != ')' && c != '"' && c != '/' && byte != 0;
}

std::size_t codePoints(std::string_view text, std::size_t bytes) noexcept
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < bytes && i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
               const auto lower = [](char c) {
                   return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
               };
               return lower(x) == lower(y);
           });
}

}  // namespace

// Tokenizer and recursive descent parser (SPEC §5.1).
class BoolExprParser
{
public:
    BoolExprParser(std::string_view text, BoolExprOptions options)
        : m_text(text)
        , m_options(options)
    {}

    tl::expected<std::unique_ptr<BoolExpr>, ExprError> run()
    {
        if (auto tokens = tokenize(); !tokens) {
            return tl::unexpected(tokens.error());
        }
        if (m_tokens.front().kind == TokenKind::kEnd) {
            return fail("empty expression", 0);
        }
        auto expr = std::unique_ptr<BoolExpr>(new BoolExpr());
        m_expr = expr.get();
        auto root = parseOr();
        if (!root) {
            return tl::unexpected(root.error());
        }
        const Token& next = peek();
        if (next.kind == TokenKind::kRParen) {
            return fail("unmatched ')'", next.offset);
        }
        if (next.kind != TokenKind::kEnd) {
            return fail("expected AND or OR", next.offset);
        }
        expr->m_root = *root;
        return expr;
    }

private:
    using NodeResult = tl::expected<std::size_t, ExprError>;

    [[nodiscard]] tl::unexpected<ExprError> fail(std::string message, std::size_t byteOffset) const
    {
        return tl::unexpected(
            ExprError{.message = std::move(message), .position = codePoints(m_text, byteOffset)});
    }

    tl::expected<void, ExprError> tokenize()
    {
        std::size_t i = 0;
        while (true) {
            while (i < m_text.size() && isSpace(m_text[i])) {
                ++i;
            }
            if (i >= m_text.size()) {
                m_tokens.push_back(makeToken(TokenKind::kEnd, i));
                return {};
            }
            const std::size_t start = i;
            const char c = m_text[i];
            if (c == '(' || c == ')') {
                m_tokens.push_back(
                    makeToken(c == '(' ? TokenKind::kLParen : TokenKind::kRParen, i));
                ++i;
            } else if (c == '"') {
                auto token = readLiteral(i);
                if (!token) {
                    return tl::unexpected(token.error());
                }
                m_tokens.push_back(std::move(*token));
            } else if (c == '/') {
                auto token = readRegex(i);
                if (!token) {
                    return tl::unexpected(token.error());
                }
                m_tokens.push_back(std::move(*token));
            } else {
                while (i < m_text.size() && isWordChar(m_text[i])) {
                    ++i;
                }
                const std::string_view word = m_text.substr(start, i - start);
                TokenKind kind = TokenKind::kEnd;
                if (equalsIgnoreCase(word, "and")) {
                    kind = TokenKind::kAnd;
                } else if (equalsIgnoreCase(word, "or")) {
                    kind = TokenKind::kOr;
                } else if (equalsIgnoreCase(word, "not")) {
                    kind = TokenKind::kNot;
                } else {
                    return fail("unknown word '" + std::string(word) +
                                    "': put text in quotes or use AND, OR, NOT",
                                start);
                }
                m_tokens.push_back(makeToken(kind, start));
            }
        }
    }

    // A term must be followed by whitespace, a parenthesis, or the end.
    tl::expected<void, ExprError> checkTermEnd(std::size_t i) const
    {
        if (i < m_text.size() && isWordChar(m_text[i])) {
            return fail("expected whitespace or a parenthesis after the term", i);
        }
        return {};
    }

    tl::expected<Token, ExprError> readLiteral(std::size_t& i) const
    {
        Token token = makeToken(TokenKind::kLiteral, i);
        const std::size_t start = i;
        ++i;
        while (true) {
            if (i >= m_text.size()) {
                return fail("unterminated literal: missing closing '\"'", start);
            }
            const char c = m_text[i];
            if (c == '"') {
                ++i;
                break;
            }
            if (c == '\\') {
                if (i + 1 >= m_text.size()) {
                    return fail("unterminated literal: missing closing '\"'", start);
                }
                const char next = m_text[i + 1];
                if (next != '"' && next != '\\') {
                    return fail(R"(invalid escape in literal: use \" or \\)", i);
                }
                token.text += next;
                i += 2;
                continue;
            }
            token.text += c;
            ++i;
        }
        token.source = std::string(m_text.substr(start, i - start));
        if (auto end = checkTermEnd(i); !end) {
            return tl::unexpected(end.error());
        }
        return token;
    }

    tl::expected<Token, ExprError> readRegex(std::size_t& i) const
    {
        Token token = makeToken(TokenKind::kRegex, i);
        const std::size_t start = i;
        ++i;
        while (true) {
            if (i >= m_text.size()) {
                return fail("unterminated regex: missing closing '/'", start);
            }
            const char c = m_text[i];
            if (c == '/') {
                ++i;
                break;
            }
            if (c == '\\') {
                if (i + 1 >= m_text.size()) {
                    return fail("unterminated regex: missing closing '/'", start);
                }
                if (m_text[i + 1] == '/') {
                    token.text += '/';
                } else {
                    token.text += m_text.substr(i, 2);
                }
                i += 2;
                continue;
            }
            token.text += c;
            ++i;
        }
        // Flags: only `i`.
        const std::size_t flagStart = i;
        while (i < m_text.size() && isWordChar(m_text[i])) {
            if (m_text[i] != 'i' || i > flagStart) {
                return fail("unknown regex flag '" + std::string(1, m_text[i]) + "' (only i)", i);
            }
            token.caseless = true;
            ++i;
        }
        token.source = std::string(m_text.substr(start, i - start));
        return token;
    }

    [[nodiscard]] const Token& peek() const { return m_tokens[m_pos]; }

    const Token& take() { return m_tokens[m_pos++]; }

    std::size_t addNode(BoolExpr::Node node)
    {
        m_expr->m_nodes.push_back(std::move(node));
        return m_expr->m_nodes.size() - 1;
    }

    // Builds an n-ary AND/OR node, or returns the single child.
    std::size_t combine(BoolExpr::Kind kind, std::vector<std::size_t> children)
    {
        if (children.size() == 1) {
            return children.front();
        }
        BoolExpr::Node node;
        node.kind = kind;
        node.children = std::move(children);
        node.order = node.children;
        // Literal-only subtrees first (stable: keeps source order among equals). AND and OR are
        // commutative for line matching, so this does not change the result.
        std::ranges::stable_sort(node.order, [this](std::size_t a, std::size_t b) {
            return !m_expr->isRegexHeavy(a) && m_expr->isRegexHeavy(b);
        });
        return addNode(std::move(node));
    }

    NodeResult parseOr()
    {
        std::vector<std::size_t> children;
        auto first = parseAnd();
        if (!first) {
            return first;
        }
        children.push_back(*first);
        while (peek().kind == TokenKind::kOr) {
            take();
            auto next = parseAnd();
            if (!next) {
                return next;
            }
            children.push_back(*next);
        }
        return combine(BoolExpr::Kind::kOr, std::move(children));
    }

    NodeResult parseAnd()
    {
        std::vector<std::size_t> children;
        auto first = parseNot();
        if (!first) {
            return first;
        }
        children.push_back(*first);
        while (peek().kind == TokenKind::kAnd) {
            take();
            auto next = parseNot();
            if (!next) {
                return next;
            }
            children.push_back(*next);
        }
        return combine(BoolExpr::Kind::kAnd, std::move(children));
    }

    NodeResult parseNot()
    {
        if (peek().kind == TokenKind::kNot) {
            take();
            auto operand = parseNot();
            if (!operand) {
                return operand;
            }
            BoolExpr::Node node;
            node.kind = BoolExpr::Kind::kNot;
            node.children = {*operand};
            node.order = node.children;
            return addNode(std::move(node));
        }
        return parsePrimary();
    }

    NodeResult parsePrimary()
    {
        const Token& token = peek();
        switch (token.kind) {
        case TokenKind::kLParen: {
            const std::size_t open = token.offset;
            take();
            if (peek().kind == TokenKind::kRParen) {
                return fail("empty parentheses", peek().offset);
            }
            auto inner = parseOr();
            if (!inner) {
                return inner;
            }
            if (peek().kind != TokenKind::kRParen) {
                if (peek().kind == TokenKind::kEnd) {
                    return fail("unmatched '('", open);
                }
                return fail("expected AND, OR, or ')'", peek().offset);
            }
            take();
            return inner;
        }
        case TokenKind::kLiteral: {
            const Token& term = take();
            BoolExpr::Node node;
            node.matcher = makeLiteralMatcher(term.text, m_options.caseSensitive, false);
            node.source = term.source;
            return addNode(std::move(node));
        }
        case TokenKind::kRegex: {
            const Token& term = take();
            auto regex = RegexMatcher::compile(
                term.text,
                RegexOptions{.caseSensitive = m_options.caseSensitive && !term.caseless});
            if (!regex) {
                // Offset inside the pattern; +1 for the opening '/'. Escaped slashes shift it
                // slightly, which is acceptable for an error marker.
                return fail("invalid regex: " + regex.error().message,
                            term.offset + 1 + regex.error().offset);
            }
            BoolExpr::Node node;
            node.matcher = std::move(*regex);
            node.source = term.source;
            node.regex = true;
            return addNode(std::move(node));
        }
        case TokenKind::kRParen:
            return fail(m_pos == 0 ? "unmatched ')'" : "missing term before ')'", token.offset);
        case TokenKind::kEnd:
            return fail("missing term at the end", token.offset);
        case TokenKind::kAnd:
        case TokenKind::kOr:
        case TokenKind::kNot:
            break;
        }
        return fail("missing term before operator", token.offset);
    }

    std::string_view m_text;
    BoolExprOptions m_options;
    std::vector<Token> m_tokens;
    std::size_t m_pos = 0;
    BoolExpr* m_expr = nullptr;
};

tl::expected<std::unique_ptr<BoolExpr>, ExprError> BoolExpr::parse(std::string_view text,
                                                                   BoolExprOptions options)
{
    return BoolExprParser(text, options).run();
}

BoolExpr::~BoolExpr() = default;

bool BoolExpr::isRegexHeavy(std::size_t node) const
{
    const Node& n = m_nodes[node];
    if (n.kind == Kind::kTerm) {
        return n.regex;
    }
    return std::ranges::any_of(n.children,
                               [this](std::size_t child) { return isRegexHeavy(child); });
}

bool BoolExpr::evaluate(std::size_t node, std::string_view line, MatchScratch& scratch) const
{
    const Node& n = m_nodes[node];
    switch (n.kind) {
    case Kind::kTerm:
        return n.matcher->matches(line, scratch);
    case Kind::kNot:
        return !evaluate(n.children.front(), line, scratch);
    case Kind::kAnd:
        return std::ranges::all_of(
            n.order, [&](std::size_t child) { return evaluate(child, line, scratch); });
    case Kind::kOr:
        return std::ranges::any_of(
            n.order, [&](std::size_t child) { return evaluate(child, line, scratch); });
    }
    return false;
}

bool BoolExpr::matches(std::string_view line, MatchScratch& scratch) const
{
    return evaluate(m_root, line, scratch);
}

void BoolExpr::collect(std::size_t node, bool negated, std::string_view line, MatchScratch& scratch,
                       std::vector<MatchRange>& out) const
{
    const Node& n = m_nodes[node];
    if (n.kind == Kind::kTerm) {
        if (!negated) {
            n.matcher->collectRanges(line, scratch, out);
        }
        return;
    }
    const bool childNegated = n.kind == Kind::kNot ? !negated : negated;
    for (const std::size_t child : n.children) {
        collect(child, childNegated, line, scratch, out);
    }
}

void BoolExpr::collectRanges(std::string_view line, MatchScratch& scratch,
                             std::vector<MatchRange>& out) const
{
    std::vector<MatchRange> ranges;
    collect(m_root, false, line, scratch, ranges);
    std::ranges::sort(ranges, [](const MatchRange& x, const MatchRange& y) {
        return x.begin < y.begin || (x.begin == y.begin && x.end > y.end);
    });
    std::vector<MatchRange> merged;
    for (const MatchRange& range : ranges) {
        if (!merged.empty() && range.begin <= merged.back().end) {
            merged.back().end = std::max(merged.back().end, range.end);
        } else {
            merged.push_back(range);
        }
    }
    out.insert(out.end(), merged.begin(), merged.end());
}

std::string BoolExpr::toString(std::size_t node) const
{
    const Node& n = m_nodes[node];
    switch (n.kind) {
    case Kind::kTerm:
        return n.source;
    case Kind::kNot:
        return "(NOT " + toString(n.children.front()) + ")";
    case Kind::kAnd:
    case Kind::kOr: {
        std::string text = n.kind == Kind::kAnd ? "(AND" : "(OR";
        for (const std::size_t child : n.children) {
            text += ' ';
            text += toString(child);
        }
        return text + ")";
    }
    }
    return {};
}

std::string BoolExpr::toString() const
{
    return toString(m_root);
}

}  // namespace lv::core
