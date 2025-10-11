#include "Token.hpp"
#include "SyntaxParser.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <sstream>

using std::string;

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

TokenType classifyIdentifier(const string& ident)
{
    auto it = keywordMap.find(ident);
    return (it != keywordMap.end()) ? it->second : TokenType::Identifier;
}

std::vector<Token> lexSource(const string& source)
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
                if (c == '/'&& i + 1 < source.size()&& source[i + 1] == '/')
                {
                    i += 2;
                    while (i < source.size()&& source[i] != '\n') ++i;
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
                if (!atEnd&& (std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
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
                if (!atEnd&& std::isdigit(static_cast<unsigned char>(c)))
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

    out.push_back(Token{"", TokenType::EndOfFile});
    return out;
}

int fail(int code, const string& message)
{
    std::cerr << message << std::endl;
    return code;
}

static string parseFactor(const FactorNode& factor, IRContext& ctx)
{
    if (const auto* id = dynamic_cast<const IDNode*>(&factor))
    {
        auto it = ctx.vars.find(id->name());
        if (it == ctx.vars.end())
        {
            fail(4, "Error: undeclared variable " + id->name());
            return "";
        }

        string tmp = "t" + std::to_string(ctx.tempId++);
        ctx.ir << "  %" << tmp << " = load i32, i32* %" << id->name() << "\n";
        return "%" + tmp;
    }

    if (const auto* number = dynamic_cast<const NumberNode*>(&factor))
    {
        return std::to_string(number->value());
    }

    fail(8, "Error: unsupported factor in expression");
    return "";
}

static string parseExpr(const ExprNode& expr, IRContext& ctx)
{
    if (const auto* factor = dynamic_cast<const FactorNode*>(&expr))
        return parseFactor(*factor, ctx);

    if (const auto* bin = dynamic_cast<const BinaryOpNode*>(&expr))
    {
        string leftVal = parseExpr(*bin->left(), ctx);
        if (leftVal.empty())
            return "";

        string rightVal = parseExpr(*bin->right(), ctx);
        if (rightVal.empty())
            return "";

        const char* opInstr = "add";
        switch (bin->op())
        {
            case BinaryOpNode::Operator::Add: opInstr = "add"; break;
            case BinaryOpNode::Operator::Sub: opInstr = "sub"; break;
            case BinaryOpNode::Operator::Mul: opInstr = "mul"; break;
        }

        string tmp = "t" + std::to_string(ctx.tempId++);
        ctx.ir << "  %" << tmp << " = " << opInstr << " i32 " << leftVal << ", " << rightVal << "\n";
        return "%" + tmp;
    }

    fail(8, "Error: unsupported expression node");
    return "";
}

static int parseDecl(const DeclNode& decl, IRContext& ctx)
{
    const std::string& name = decl.identifier();
    if (ctx.vars.count(name))
        return fail(3, "Error: variable " + name + " is already declared");

    ctx.ir << "  %" << name << " = alloca i32\n";
    ctx.vars[name] = decl.isMutable();

    if (decl.initializer())
    {
        string initVal = parseExpr(*decl.initializer(), ctx);
        if (initVal.empty())
            return fail(8, "Error: invalid initializer for variable " + name);
        ctx.ir << "  store i32 " << initVal << ", i32* %" << name << "\n";
    }
    else
    {
        ctx.ir << "  store i32 0, i32* %" << name << "\n";
    }

    return 0;
}

static int parseAssign(const AssignNode& assign, IRContext& ctx)
{
    const std::string& name = assign.identifier();
    auto it = ctx.vars.find(name);
    if (it == ctx.vars.end())
        return fail(4, "Error: undeclared variable " + name);
    if (!it->second)
        return fail(16, "Error: variable " + name + " is immutable");

    string value = parseExpr(*assign.value(), ctx);
    if (value.empty())
        return fail(8, "Error: invalid expression in assignment to " + name);

    ctx.ir << "  store i32 " << value << ", i32* %" << name << "\n";
    return 0;
}

static int parseReturn(const ReturnNode& ret, IRContext& ctx)
{
    if (!ret.expr())
        return fail(6, "Error: expected expression after 'return'");

    string result = parseExpr(*ret.expr(), ctx);
    if (result.empty())
        return fail(7, "Error: invalid return expression");

    if (result.front() != '%')
    {
        string tmp = "t" + std::to_string(ctx.tempId++);
        ctx.ir << "  %" << tmp << " = add i32 0, " << result << "\n";
        result = "%" + tmp;
    }

    ctx.ir << "  %fmtptr = getelementptr [29 x i8], [29 x i8]* @fmt, i32 0, i32 0\n";
    ctx.ir << "  call i32 (i8*, ...) @printf(i8* %fmtptr, i32 " << result << ")\n";
    ctx.ir << "  ret i32 " << result << "\n";
    return 0;
}

static int parseProgram(const ProgramNode& program, IRContext& ctx)
{
    for (const auto& stmt : program.statements())
    {
        if (!stmt)
            continue;

        if (const auto* decl = dynamic_cast<const DeclNode*>(stmt.get()))
        {
            if (int code = parseDecl(*decl, ctx))
                return code;
            continue;
        }

        if (const auto* assign = dynamic_cast<const AssignNode*>(stmt.get()))
        {
            if (int code = parseAssign(*assign, ctx))
                return code;
            continue;
        }

        return fail(1, "Error: unsupported statement node encountered");
    }

    const ReturnNode* ret = program.returnStmt();
    if (!ret)
        return fail(5, "Error: missing return statement");

    return parseReturn(*ret, ctx);
}

int main(int argc, char** argv)
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
    SyntaxParser parser(tokens);
    auto program = parser.parseProgram();

    if (!program || parser.hasErrors())
    {
        const auto& errs = parser.errors();
        if (errs.empty())
        {
            std::cerr << "Parse error: unable to build AST" << std::endl;
        }
        else
        {
            for (const auto& err : errs)
                std::cerr << "Parse error: " << err << std::endl;
        }
        return 1;
    }

    IRContext ctx;

    ctx.ir << "declare i32 @printf(i8*, ...)\n\n";
    ctx.ir << "@fmt = private constant [29 x i8] c\"Program exit with result %d\\0A\\00\"\n\n";
    ctx.ir << "define i32 @main() {\n";

    if (int code = parseProgram(*program, ctx))
        return code;

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