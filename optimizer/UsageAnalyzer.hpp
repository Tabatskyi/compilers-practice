#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../ast/AssignFieldNode.hpp"
#include "../ast/AssignNode.hpp"
#include "../ast/BinaryOpNode.hpp"
#include "../ast/BlockNode.hpp"
#include "../ast/BoolLiteralNode.hpp"
#include "../ast/DeclNode.hpp"
#include "../ast/FieldAccessNode.hpp"
#include "../ast/FunctionCallNode.hpp"
#include "../ast/FunctionNode.hpp"
#include "../ast/IDNode.hpp"
#include "../ast/IfNode.hpp"
#include "../ast/MemberFunctionCallNode.hpp"
#include "../ast/NumberNode.hpp"
#include "../ast/ProgramNode.hpp"
#include "../ast/ReturnNode.hpp"
#include "../ast/StructDeclNode.hpp"
#include "../ast/UnaryOpNode.hpp"
#include "../ast/TypeDesc.hpp"

class UsageAnalyzer
{
public:
    void analyze(ProgramNode& program)
    {
        visitProgram(program);
    }

    const std::unordered_map<SymbolID, std::size_t>& usages() const { return usageCount; }
    const std::unordered_set<SymbolID>& declarations() const { return declaredSymbols; }

    static bool pruneProgram(ProgramNode& program, const std::unordered_set<SymbolID>& unusedIds);

private:
    void visitProgram(const ProgramNode& program);
    void visitBlock(const BlockNode& block);
    void visitStruct(const StructDeclNode& node);
    void visitFunction(const FunctionNode& node);
    void visitStmt(const StmtNode& stmt);
    void visitExpr(const ExprNode* expr);

    void recordUsage(SymbolID id);
    void recordFieldAccess(const FieldAccessNode& access);

    template <typename StmtList>
    static bool pruneStatementList(StmtList& statements, const std::unordered_set<SymbolID>& unusedIds);
    static bool pruneBlock(BlockNode& block, const std::unordered_set<SymbolID>& unusedIds);
    static bool pruneStmt(StmtNode& stmt, const std::unordered_set<SymbolID>& unusedIds);

    std::unordered_map<SymbolID, std::size_t> usageCount;
    std::unordered_set<SymbolID> declaredSymbols;
};