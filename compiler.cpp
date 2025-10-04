#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <regex>
#include <cctype>
#include <sstream>

using std::string; 
using std::string_view;

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
    std::unordered_map<string,Var> vars;
    int tempId = 0;
    std::ostringstream ir;
} ctx;

string trim(const string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if(a == string::npos) 
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
};

bool isDigits(const string &t)
{ 
    if(t.empty()) 
        return false; 
    for(char c: t) 
        if(!std::isdigit((unsigned char)c)) 
            return false; 
    return true; 
};

string lowerExpr(const string &exprRaw, int curLine, struct IRContext &ctx)
{
    string expr = trim(exprRaw);
    if(expr.empty()) throw std::runtime_error("Empty expression at line " + std::to_string(curLine));

    std::vector<string> parts; size_t start=0; 
    while(true)
    {
        size_t pos = expr.find('+', start);
        if(pos == string::npos)
        { 
            parts.push_back(trim(expr.substr(start))); 
            break; 
        }
        parts.push_back(trim(expr.substr(start, pos - start)));
        start = pos + 1;
    }
    if(parts.empty()) throw std::runtime_error("Malformed expression at line "+std::to_string(curLine));

    string acc; 
    bool haveAcc = false;
    for(const string &t: parts)
    {
        string val;
        if(isDigits(t)) 
        {
            val = t; 
        } 
        else 
        {
            if(!std::regex_match(t, std::regex(R"([A-Za-z_][A-Za-z0-9_]*)")))
                throw std::runtime_error("Invalid token '" + t + "' at line " + std::to_string(curLine));
            if(!ctx.vars.count(t))
                throw std::runtime_error("Use of undeclared variable '" + t + "' at line " + std::to_string(curLine));
            string tmp = "t" + std::to_string(ctx.tempId++);
            ctx.ir << "  %" << tmp << " = load i32, i32* %" << t << "\n";
            val = "%" + tmp;
        }
        if(!haveAcc) 
        { 
            acc = val; 
            haveAcc = true; 
            continue; 
        }
        string tmpAdd = "t" + std::to_string(ctx.tempId++);
        ctx.ir << "  %" << tmpAdd << " = add i32 " << acc << ", " << val << "\n";
        acc = "%" + tmpAdd;
    }
    return acc; 
};

int main(int argc, char** argv) 
{
    std::ifstream fin(argv[1]); 
    if(!fin)
    { 
        std::cerr << "Cannot open file\n"; 
        return 1; 
    }

    std::string filename = argv[1];
    filename = filename.substr(0, filename.find_last_of('.'));
    std::string line; 
    int lineNo = 0; 
    bool sawReturn = false;

    std::regex declRe(R"(^\s*var\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
    std::regex assignRe(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*$)");
    std::regex returnRe(R"(^\s*return\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
    
    ctx.ir << "declare i32 @printf(i8*, ...)\n\n";
    ctx.ir << "@fmt = private constant [29 x i8] c\"Program exit with result %d\\0A\\00\"\n\n";
    ctx.ir << "define i32 @main() {\n";

    while (std::getline(fin, line)) {
        ++lineNo;
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        std::smatch m;
        if (std::regex_match(line, m, declRe)) {
            if (sawReturn) throw std::runtime_error("Statements after return at line " + std::to_string(lineNo));
            {
                string var = m[1];
                if (ctx.vars.count(var)) throw std::runtime_error("Variable re-declaration at line " + std::to_string(lineNo));
                ctx.ir << "  %" << var << " = alloca i32\n";
                ctx.vars[var] = Var{var};
                continue;
            }
        }
        if (std::regex_match(line, m, assignRe)) {
            if(sawReturn) throw std::runtime_error("Statements after return at line " + std::to_string(lineNo));
            {
                auto var = m[1];
                auto expr = m[2];
                if(!ctx.vars.count(var)) throw std::runtime_error("Undeclared variable at line " + std::to_string(lineNo));
                string val = lowerExpr(expr, lineNo, ctx);
                ctx.ir << "  store i32 " << val << ", i32* %" << var << "\n";
                continue;
            }
        }
        if (std::regex_match(line, m, returnRe)) {
            if(sawReturn) throw std::runtime_error("Multiple return statements (line " + std::to_string(lineNo) + ")");
            sawReturn = true;
            {
                auto var = m[1];
                if(!ctx.vars.count(var)) throw std::runtime_error("Return of undeclared variable at line "+std::to_string(lineNo));
                ctx.ir << "  %retv = load i32, i32* %" << var << "\n";
                ctx.ir << "  %fmtptr = getelementptr [29 x i8], [29 x i8]* @fmt, i32 0, i32 0\n";
                ctx.ir << "  call i32 (i8*, ...) @printf(i8* %fmtptr, i32 %retv)\n";
                ctx.ir << "  ret i32 %retv\n";
                continue;
            }
        }
        throw std::runtime_error("Syntax error line " + std::to_string(lineNo)+": " + line);
    }
    if(!sawReturn) 
    { 
        std::cerr<<"Error: no return statement\n"; 
        return 2; 
    }

    ctx.ir << "}\n";
    fin.close();

    std::ofstream fout(filename + ".ll");
    fout << ctx.ir.str();
    fout.close();

    return 0;
}