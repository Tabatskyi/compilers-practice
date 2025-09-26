#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <regex>
#include <cctype>

using std::string; 
using std::string_view;

int main(int argc, char** argv) 
{
    std::ifstream fin(argv[1]); 
    if(!fin)
    { 
        std::cerr<<"Cannot open file\n"; 
        return 1; 
    }

    std::string filename = argv[1];
    filename = filename.substr(0, filename.find_last_of('.'));
    std::string line; 
    int lineNo=0; 
    bool sawReturn=false;

    std::regex declRe(R"(^\s*var\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
    std::regex assignRe(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*$)");
    std::regex returnRe(R"(^\s*return\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");

    struct Var { string allocaName; };
    struct IRContext {
        std::unordered_map<string,Var> vars;
        int tempId = 0;
        std::ostringstream ir;
    } ctx;
    
    ctx.ir << "declare i32 @printf(i8*, ...)\n\n";
    ctx.ir << "@fmt = private constant [29 x i8] c\"Program exit with result %d\\0A\\00\"\n\n";
    ctx.ir << "define i32 @main() {\n";
    

    while (std::getline(fin, line)) {
        ++lineNo;
        if (line.find_first_not_of(" \t\r\n")==std::string::npos) continue;
        std::smatch m;
        if (std::regex_match(line, m, declRe)) {
            if(sawReturn) throw std::runtime_error("Statements after return at line " + std::to_string(lineNo));
            {
                string var = m[1]; string exprTxt = m[2];
                if(ctx.vars.count(var)) throw std::runtime_error("Variable re-declaration at line " + std::to_string(lineNo));
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
                ctx.ir << "  store i32 " << expr << ", i32* %" << var << "\n";
                continue;
            }
        }
        if (std::regex_match(line, m, returnRe)) {
            if(sawReturn) throw std::runtime_error("Multiple return statements (line " + std::to_string(lineNo) + ")");
            sawReturn = true;
            {
                auto var = m[1];
                ctx.ir << "  %retv = load i32, i32* %" << var << "\n";
                ctx.ir << "  %fmtptr = getelementptr [31 x i8], [31 x i8]* @fmt, i32 0, i32 0\n";
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


    std::ofstream fout(filename + ".ll");
    fout << ctx.ir.str();
    fout.close();

    return 0;
}