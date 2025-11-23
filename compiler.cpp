#include "generator/CodeGen.hpp"
#include "lexer/Lexer.hpp"
#include "parser/SyntaxParser.hpp"
#include "semantic/Semantic.hpp"
#include "semantic/SemanticAnalyzer.hpp"
#include "optimizer/UnusedSymbolCleaner.hpp"
#include "general/Diagnostics.hpp"

#include <limits>
#include <sstream>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using std::string;

constexpr const char* kColorRed = "\033[31m";
constexpr const char* kColorGreen = "\033[32m";
constexpr const char* kColorReset = "\033[0m";

void printStageError(const std::string& stage, const std::string& message, std::size_t line) 
{
    std::cerr << kColorRed << "❌ " << stage << " error: " << message;
    if (line != 0)
        std::cerr << " at line " << line;
    std::cerr << kColorReset << std::endl;
};

void printSuccess(const std::string& message) 
{
    std::cout << kColorGreen << "✅ " << message << kColorReset << std::endl;
};

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
        std::cerr << kColorRed << "❌ IO error: cannot open file '" << argv[1] << "'" << kColorReset << std::endl;
        return 1;
    }

    std::ostringstream buffer;
    buffer << fin.rdbuf();
    string source = buffer.str();
    fin.close();

    Lexer lexer;
    LexResult lexResult = lexer.tokenize(source);
    if (!lexResult.errors.empty())
    {
        for (const auto& err : lexResult.errors)
            printStageError("Lexer", err.message, err.line);
        return 1;
    }
    printSuccess("Lexing completed.");

    SyntaxParser parser(std::move(lexResult.tokens));
    std::unique_ptr<ProgramNode> program = parser.parseProgram();

    if (!program || parser.hasErrors())
    {
        const std::vector<Diagnostic>& errs = parser.errors();
        if (errs.empty())
        {
            printStageError("Syntax", "unable to build AST", 0);
        }
        else
        {
            for (const Diagnostic& err : errs)
                printStageError("Syntax", err.message, err.line);
        }
        return 1;
    }

    printSuccess("Syntax parsing done.");

    SemanticAnalyzer semantic;
    if (!semantic.analyze(*program))
    {
        for (const auto& err : semantic.errors())
            printStageError("Semantics", err.message, err.line);
        return 1;
    }

    printSuccess("Semantic analysis done.");

    UnusedSymbolCleaner cleaner;
    cleaner.removeUnusedVariables(*program);

    IRContext ctx;
    ctx.ir << "declare i32 @printf(i8*, ...)\n\n";
    ctx.ir << "@fmt = private constant [29 x i8] c\"Program exit with result %d\\0A\\00\"\n\n";

    CodeGenerator generator(ctx, semantic.symbols(), semantic.structs(), semantic.functions());
    generator.emitTopLevel(*program);

    ctx.ir << "define i32 @main() {\n";
    generator.generate(*program);
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
        if (dot != string::npos)
            filename = filename.substr(0, dot);
        filename += ".ll";
    }

    std::ofstream fout(filename);
    if (!fout)
    {
        std::cerr << kColorRed << "❌ IO error: cannot write output file '" << filename << "'" << kColorReset << std::endl;
        return 1;
    }
    fout << ctx.ir.str();
    fout.close();

    printSuccess("Compilation completed successfully.");
    return 0;
}