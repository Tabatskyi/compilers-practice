#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <sstream>

using std::string;

enum class TokenType
{
    Newline,
    Identifier,
    Number,
    Var,
    Mut,
    Return,
    I32,
    BlockStart,
    BlockEnd,
    Assign,
    Add,
    Sub,
    Mul
};

struct Token
{
    string lexeme;
    TokenType type;
};

struct IRContext
{
    std::unordered_map<string, bool> vars;
    int tempId = 0;
    std::ostringstream ir;
};

static const std::unordered_map<string, TokenType> keywordMap =
{
    {"var", TokenType::Var},
    {"mut", TokenType::Mut},
    {"return", TokenType::Return},
    {"i32", TokenType::I32}
};

TokenType classifyIdentifier(const string &ident)
{
    auto it = keywordMap.find(ident);
    return (it != keywordMap.end()) ? it->second : TokenType::Identifier;
}

std::vector<Token> lexSource(const string &source)
{
    std::vector<Token> out;
    enum class State { Start, Identifier, Number };
    State state = State::Start;
    string buffer;

    for (size_t i = 0; i <= source.size(); )
    {
        char c = (i < source.size()) ? source[i] : '\0';
        bool atEnd = (i == source.size());
        TokenType kind;

        switch (state)
        {
            case State::Start:
                if (atEnd)
                {
                    ++i;
                    continue;
                }
                if (c == '\n')
                {
                    out.push_back(Token{"\n", TokenType::Newline});
                    ++i;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(c)))
                {
                    ++i;
                    continue;
                }
                if (c == '/' && i + 1 < source.size() && source[i + 1] == '/')
                {
                    i += 2;
                    while (i < source.size() && source[i] != '\n') ++i;
                    continue;
                }
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
                {
                    buffer.assign(1, c);
                    state = State::Identifier;
                    ++i;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c)))
                {
                    buffer.assign(1, c);
                    state = State::Number;
                    ++i;
                    continue;
                }

                kind = TokenType::Newline;
                switch (c)
                {
                    case '{': kind = TokenType::BlockStart; break;
                    case '}': kind = TokenType::BlockEnd; break;
                    case '=': kind = TokenType::Assign; break;
                    case '+': kind = TokenType::Add; break;
                    case '-': kind = TokenType::Sub; break;
                    case '*': kind = TokenType::Mul; break;
                    default: ++i; continue;
                }
                out.push_back(Token{string(1, c), kind});
                ++i;
                continue;

            case State::Identifier:
                if (!atEnd && (std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                {
                    buffer.push_back(c);
                    ++i;
                    continue;
                }
                out.push_back(Token{buffer, classifyIdentifier(buffer)});
                buffer.clear();
                state = State::Start;
                continue;

            case State::Number:
                if (!atEnd && std::isdigit(static_cast<unsigned char>(c)))
                {
                    buffer.push_back(c);
                    ++i;
                    continue;
                }
                out.push_back(Token{buffer, TokenType::Number});
                buffer.clear();
                state = State::Start;
                continue;
        }
    }

    return out;
}

string lowerExpr(const std::vector<Token> &exprTokens, IRContext &ctx)
{
    string acc;
    for (size_t i = 0; i < exprTokens.size(); ++i)
    {
        const Token &token = exprTokens[i];
        if ((i & 1) == 0)
        {
            string val;
            if (token.type == TokenType::Number)
            {
                val = token.lexeme;
            }
            else if (token.type == TokenType::Identifier)
            {
                string tmp = "t" + std::to_string(ctx.tempId++);
                ctx.ir << "  %" << tmp << " = load i32, i32* %" << token.lexeme << "\n";
                val = "%" + tmp;
            }
            else
            {
                return "";
            }

            if (i == 0)
            {
                acc = val;
            }
            else
            {
                string tmpAdd = "t" + std::to_string(ctx.tempId++);
                ctx.ir << "  %" << tmpAdd << " = add i32 " << acc << ", " << val << "\n";
                acc = "%" + tmpAdd;
            }
        }
        else if (token.type != TokenType::Add)
        {
            return "";
        }
    }
    return acc;
}

int fail(int code, const string &message)
{
    std::cerr << message << std::endl;
    return code;
}

int expectNewline(const string &name, size_t &idx, const std::vector<Token> &tokens)
{
    if (idx < tokens.size())
    {
        if (tokens[idx].type != TokenType::Newline)
            return fail(10, "Error: expected newline after declaration of " + name);
        ++idx;
    }
    return 0;
}

int declare(const Token &nameTok, bool isMutable, IRContext &ctx)
{
    if (ctx.vars.count(nameTok.lexeme))
        return fail(3, "Error: variable " + nameTok.lexeme + " is already declared");
    ctx.ir << "  %" << nameTok.lexeme << " = alloca i32\n";
    ctx.vars[nameTok.lexeme] = isMutable;
    return 0;
}

std::vector<Token> collectExpr(size_t &idx, const std::vector<Token> &tokens)
{
    std::vector<Token> expr;
    while (idx < tokens.size() && tokens[idx].type != TokenType::Newline)
        expr.push_back(tokens[idx++]);
    if (idx < tokens.size() && tokens[idx].type == TokenType::Newline) ++idx;
    return expr;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: compiler <source> <output>" << std::endl;
        return 1;
    }

    std::ifstream fin(argv[1]);
    if (!fin)
    {
        std::cerr << "Cannot open file\n";
        return 1;
    }

    std::ostringstream buffer;
    buffer << fin.rdbuf();
    string source = buffer.str();
    fin.close();

    auto tokens = lexSource(source);
    IRContext ctx;

    ctx.ir << "declare i32 @printf(i8*, ...)\n\n";
    ctx.ir << "@fmt = private constant [29 x i8] c\"Program exit with result %d\\0A\\00\"\n\n";
    ctx.ir << "define i32 @main() {\n";

    size_t idx = 0;
    bool sawReturn = false;

    while (true)
    {
        while (idx < tokens.size() && tokens[idx].type == TokenType::Newline) ++idx;
        if (idx >= tokens.size()) break;

        const Token &token = tokens[idx];

        switch (token.type)
        {
            case TokenType::Var:
            {
                if (sawReturn) return fail(3, "Error: code after return statement");
                ++idx;
                if (idx >= tokens.size() || tokens[idx].type != TokenType::Identifier)
                    return fail(2, "Error: expected identifier after 'var'");

                const Token &nameTok = tokens[idx++];
                bool isMutable = false;

                bool sawType = false;
                while (idx < tokens.size())
                {
                    auto t = tokens[idx].type;
                    if (t == TokenType::I32)
                    {
                        sawType = true;
                        ++idx;
                    }
                    else if (t == TokenType::Mut)
                    {
                        isMutable = true;
                        ++idx;
                    }
                    else break;
                }
                if (!sawType)
                    return fail(15, "Error: missing typename for variable " + nameTok.lexeme);

                if (int code = declare(nameTok, isMutable, ctx)) return code;

                if (int code = expectNewline(nameTok.lexeme, idx, tokens)) return code;
                continue;
            }

            case TokenType::I32:
            {
                if (sawReturn) return fail(3, "Error: code after return statement");
                ++idx;
                bool isMutable = false;
                while (idx < tokens.size() && tokens[idx].type == TokenType::Mut)
                {
                    isMutable = true;
                    ++idx;
                }

                if (idx >= tokens.size() || tokens[idx].type != TokenType::Identifier)
                    return fail(12, "Error: expected identifier after typename i32");

                const Token &nameTok = tokens[idx++];

                while (idx < tokens.size() && tokens[idx].type == TokenType::Mut)
                {
                    isMutable = true;
                    ++idx;
                }

                if (int code = declare(nameTok, isMutable, ctx)) return code;
                bool hasInitializer = false;
                if (idx < tokens.size() && tokens[idx].type == TokenType::BlockStart)
                {
                    hasInitializer = true;
                    ++idx;
                    std::vector<Token> initTokens;
                    while (idx < tokens.size() && tokens[idx].type != TokenType::BlockEnd)
                        initTokens.push_back(tokens[idx++]);
                    if (idx >= tokens.size())
                        return fail(13, "Error: missing closing '}' for initializer of " + nameTok.lexeme);
                    ++idx;
                    if (initTokens.empty())
                        return fail(14, "Error: empty initializer for variable " + nameTok.lexeme);

                    string initVal = lowerExpr(initTokens, ctx);
                    if (initVal.empty())
                        return fail(8, "Error: invalid initializer for variable " + nameTok.lexeme);
                    ctx.ir << "  store i32 " << initVal << ", i32* %" << nameTok.lexeme << "\n";
                }

                if (!hasInitializer)
                    ctx.ir << "  store i32 0, i32* %" << nameTok.lexeme << "\n";

                if (int code = expectNewline(nameTok.lexeme, idx, tokens)) return code;
                continue;
            }

            case TokenType::Return:
            {
                if (sawReturn) return fail(5, "Error: code after return statement");
                sawReturn = true;
                ++idx;
                auto exprTokens = collectExpr(idx, tokens);
                if (exprTokens.empty())
                    return fail(6, "Error: expected expression after 'return'");

                string retVal = lowerExpr(exprTokens, ctx);
                if (retVal.empty())
                    return fail(7, "Error: invalid return expression");

                if (retVal[0] != '%')
                {
                    string tmp = "t" + std::to_string(ctx.tempId++);
                    ctx.ir << "  %" << tmp << " = add i32 0, " << retVal << "\n";
                    retVal = "%" + tmp;
                }

                ctx.ir << "  %fmtptr = getelementptr [29 x i8], [29 x i8]* @fmt, i32 0, i32 0\n";
                ctx.ir << "  call i32 (i8*, ...) @printf(i8* %fmtptr, i32 " << retVal << ")\n";
                ctx.ir << "  ret i32 " << retVal << "\n";

                if (idx < tokens.size())
                    return fail(3, "Error: code after return statement");
                break;
            }

            case TokenType::Identifier:
            {
                if (sawReturn) return fail(3, "Error: code after return statement");
                auto it = ctx.vars.find(token.lexeme);
                if (it == ctx.vars.end())
                    return fail(4, "Error: undeclared variable " + token.lexeme);
                if (!it->second)
                    return fail(16, "Error: variable " + token.lexeme + " is immutable");

                string varName = token.lexeme;
                ++idx;
                if (idx >= tokens.size() || tokens[idx].type != TokenType::Assign)
                    return fail(5, "Error: expected '=' after identifier " + varName);
                ++idx;

                auto exprTokens = collectExpr(idx, tokens);
                if (exprTokens.empty())
                    return fail(6, "Error: expected expression after '='");

                string val = lowerExpr(exprTokens, ctx);
                if (val.empty())
                    return fail(8, "Error: invalid expression in assignment to " + varName);

                ctx.ir << "  store i32 " << val << ", i32* %" << varName << "\n";
                continue;
            }

            default:
                return fail(1, "Error: unexpected token " + token.lexeme);
        }

        break;
    }

    ctx.ir << "}\n";
    string filename;

    if (argc >= 3)
    {
        filename = argv[2];
    }
    else
    {
        filename = argv[1];
        size_t dot = filename.find_last_of('.');
        if (dot != string::npos) filename = filename.substr(0, dot);
        filename += ".ll";
    }

    std::ofstream fout(filename);
    fout << ctx.ir.str();
    fout.close();

    return 0;
}