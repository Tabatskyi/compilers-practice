#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <sstream>

using std::string;

struct Token
{
    string lexeme;
    string kind;
    string type;
};
std::vector<Token> tokens;

struct Var { string allocaName; };
struct IRContext 
{
    std::unordered_map<string, Var> vars;
    int tempId = 0;
    std::ostringstream ir;
} ctx;

static const std::unordered_map<string, std::pair<string, string>> keywordMap = 
{
    {"var", {"keyword", "declaration"}},
    {"mut", {"keyword", "specifier"}},
    {"return", {"keyword", "control"}},
    {"i32", {"keyword", "typename"}}
};

std::pair<string, string> classifyIdentifier(const string &ident) 
{
    auto it = keywordMap.find(ident);
    if (it != keywordMap.end()) return it->second;
    return {"identifier", "name"};
}

void lexSource(const string &source) 
{
    tokens.clear();
    enum class State { Start, Identifier, Number };

    State state = State::Start;
    string buffer;
    size_t i = 0;

    while (i <= source.size()) 
    {
        char c = (i < source.size()) ? source[i] : '\0';
        bool atEnd = (i == source.size());
        auto kindType = std::make_pair(string(), string());

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
                    buffer.clear();
                    buffer.push_back(c);
                    state = State::Identifier;
                    ++i;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c))) 
                {
                    buffer.clear();
                    buffer.push_back(c);
                    state = State::Number;
                    ++i;
                    continue;
                }
                if (c == '{') 
                {
                    tokens.push_back(Token{"{", "block", "start"});
                    ++i;
                    continue;
                }
                if (c == '}') 
                {
                    tokens.push_back(Token{"}", "block", "end"});
                    ++i;
                    continue;
                }
                if (c == '=') 
                {
                    tokens.push_back(Token{"=", "operator", "assign"});
                    ++i;
                    continue;
                }
                if (c == '+') 
                {
                    tokens.push_back(Token{"+", "operator", "add"});
                    ++i;
                    continue;
                }

            case State::Identifier:
                if (!atEnd && (std::isalnum(static_cast<unsigned char>(c)) || c == '_')) 
                {
                    buffer.push_back(c);
                    ++i;
                    continue;
                }
                kindType = classifyIdentifier(buffer);
                tokens.push_back(Token{buffer, kindType.first, kindType.second});
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
                tokens.push_back(Token{buffer, "constant", "numeric"});
                buffer.clear();
                state = State::Start;
                continue;
        }
    }
}

string lowerExpr(const std::vector<Token> &exprTokens, IRContext &ctx) 
{
    string acc;
    for (size_t i = 0; i < exprTokens.size(); ++i) 
    {
        const Token &tok = exprTokens[i];
        if (i % 2 == 0) 
        {
            string val;
            if (tok.kind == "constant" && tok.type == "numeric") 
            {
                val = tok.lexeme;
            } 
            else if (tok.kind == "identifier" && tok.type == "name") {
                
                string tmp = "t" + std::to_string(ctx.tempId++);
                ctx.ir << "  %" << tmp << " = load i32, i32* %" << tok.lexeme << "\n";
                val = "%" + tmp;
            } else 
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
        } else 
        {
            if (tok.lexeme != "+" || tok.kind != "operator")
                return "";
        }
    }
    return acc;
}

int main(int argc, char **argv)
{
    if (argc < 2) 
    {
        std::cerr << "Usage: compiler <source>\n";
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

    ctx.vars.clear();
    ctx.tempId = 0;
    ctx.ir.str("");
    ctx.ir.clear();

    lexSource(source);

    ctx.ir << "declare i32 @printf(i8*, ...)\n\n";
    ctx.ir << "@fmt = private constant [29 x i8] c\"Program exit with result %d\\0A\\00\"\n\n";
    ctx.ir << "define i32 @main() {\n";

    size_t idx = 0;
    bool sawReturn = false;

    while (true) 
    {
        if (idx >= tokens.size()) break;

        const Token &tok = tokens[idx];

        if (tok.lexeme == "var" && tok.kind == "keyword" && tok.type == "declaration") 
        {
            if (sawReturn)
            {
                std::cerr << "Error: code after return statement" << std::endl;
                return 3;
            }
            ++idx;

            if (idx >= tokens.size() || tokens[idx].kind != "identifier")
            {
                std::cerr << "Error: expected identifier after 'var'" << std::endl;
                return 2;
            }
            const Token &nameTok = tokens[idx];

            if (ctx.vars.count(nameTok.lexeme))
            {
                std::cerr << "Error: variable " << nameTok.lexeme << " is already declared" << std::endl;
                return 3;
            }
            ctx.ir << "  %" << nameTok.lexeme << " = alloca i32\n";
            ctx.vars[nameTok.lexeme] = Var{nameTok.lexeme};
            ++idx;
            continue;
        }

        if (tok.lexeme == "return" && tok.kind == "keyword" && tok.type == "control") {
            if (sawReturn)
            {
                std::cerr << "Error: code after return statement" << std::endl;
                return 5;
            }
            sawReturn = true;
            ++idx;

            std::vector<Token> exprTokens;
            while (idx < tokens.size()) 
            {
                exprTokens.push_back(tokens[idx]);
                ++idx;
            }

            if (exprTokens.empty())
            {
                std::cerr << "Error: expected expression after 'return'" << std::endl;
                return 6;
            }

            string retVal = lowerExpr(exprTokens, ctx);
            string retReg = retVal;

            if (retReg.empty())
            {
                std::cerr << "Error: invalid return expression" << std::endl;
                return 7;
            }

            if (retReg[0] != '%') 
            {
                string tmp = "t" + std::to_string(ctx.tempId++);
                ctx.ir << "  %" << tmp << " = add i32 0, " << retReg << "\n";
                retReg = "%" + tmp;
            }

            ctx.ir << "  %fmtptr = getelementptr [29 x i8], [29 x i8]* @fmt, i32 0, i32 0\n";
            ctx.ir << "  call i32 (i8*, ...) @printf(i8* %fmtptr, i32 " << retReg << ")\n";
            ctx.ir << "  ret i32 " << retReg << "\n";

            if (idx < tokens.size())
            {
                std::cerr << "Error: code after return statement" << std::endl;
                return 3;
            }
            break;
        }

        if (tok.kind == "identifier" && tok.type == "name") 
        {
            if (sawReturn)
            {
                std::cerr << "Error: code after return statement\n";
                return 3;
            }
            if (!ctx.vars.count(tok.lexeme))
            {
                std::cerr << "Error: undeclared variable " << tok.lexeme << std::endl;
                return 4;
            }

            string varName = tok.lexeme;
            ++idx;
            if (idx >= tokens.size() || tokens[idx].lexeme != "=")
            {
                std::cerr << "Error: expected '=' after identifier " << varName << std::endl;
                return 5;
            }
            ++idx;

            std::vector<Token> exprTokens;
            while (idx < tokens.size()) 
            {
                exprTokens.push_back(tokens[idx]);
                ++idx;
            }
            if (exprTokens.empty())
            {
                std::cerr << "Error: expected expression after '='" << std::endl;
                return 6;
            }

            string val = lowerExpr(exprTokens, ctx);
            ctx.ir << "  store i32 " << val << ", i32* %" << varName << "\n";
            continue;
        }

        std::cerr << "Error: unexpected token " << tok.lexeme << std::endl;
        return 1;
    }

    ctx.ir << "}\n";

    string filename = argv[1];
    size_t dot = filename.find_last_of('.');
    if (dot != string::npos) filename = filename.substr(0, dot);

    std::ofstream fout(filename + ".ll");
    fout << ctx.ir.str();
    fout.close();

    return 0;
}