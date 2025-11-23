#include "UnusedSymbolCleaner.hpp"

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
#include "../ast/StructDecNode.hpp"
#include "../ast/UnaryOpNode.hpp"
#include "../ast/TypeDesc.hpp"

namespace
{
class UsageAnalyzer
{
public:
    void analyze(const ProgramNode& program)
    {
        visitProgram(program);
    }

    const std::unordered_map<SymbolID, std::size_t>& usages() const { return usageCount; }
    const std::unordered_set<SymbolID>& declarations() const { return declaredSymbols; }

private:
    void visitProgram(const ProgramNode& program)
    {
        for (const auto& stmt : program.statements())
        {
            if (stmt)
                visitStmt(*stmt);
        }
    }

    void visitBlock(const BlockNode& block)
    {
        for (const auto& stmt : block.statements())
        {
            if (stmt)
                visitStmt(*stmt);
        }
    }

    void visitStruct(const StructDeclNode& node)
    {
        for (const auto& fn : node.functions())
        {
            if (fn && fn->body())
                visitBlock(*fn->body());
        }
    }

    void visitFunction(const FunctionNode& node)
    {
        if (node.body())
            visitBlock(*node.body());
    }

    void visitStmt(const StmtNode& stmt)
    {
        if (const auto* decl = dynamic_cast<const DeclNode*>(&stmt))
        {
            SymbolID id = decl->symbolId();
            if (id != InvalidSymbolID)
                declaredSymbols.insert(id);
            for (const auto& init : decl->initializers())
            {
                if (init)
                    visitExpr(init.get());
            }
            return;
        }
        if (const auto* assign = dynamic_cast<const AssignNode*>(&stmt))
        {
            recordUsage(assign->symbolId());
            if (assign->value())
                visitExpr(assign->value());
            return;
        }
        if (const auto* assignField = dynamic_cast<const AssignFieldNode*>(&stmt))
        {
            if (const FieldAccessNode* target = assignField->target())
                recordFieldAccess(*target);
            if (assignField->value())
                visitExpr(assignField->value());
            return;
        }
        if (const auto* ifNode = dynamic_cast<const IfNode*>(&stmt))
        {
            if (ifNode->condition())
                visitExpr(ifNode->condition());
            if (ifNode->thenBlock())
                visitBlock(*ifNode->thenBlock());
            if (ifNode->elseBlock())
                visitBlock(*ifNode->elseBlock());
            return;
        }
        if (const auto* ret = dynamic_cast<const ReturnNode*>(&stmt))
        {
            visitExpr(ret->expr());
            return;
        }
        if (const auto* func = dynamic_cast<const FunctionNode*>(&stmt))
        {
            visitFunction(*func);
            return;
        }
        if (const auto* structDecl = dynamic_cast<const StructDeclNode*>(&stmt))
        {
            visitStruct(*structDecl);
            return;
        }
        if (const auto* block = dynamic_cast<const BlockNode*>(&stmt))
        {
            visitBlock(*block);
            return;
        }
    }

    void visitExpr(const ExprNode* expr)
    {
        if (!expr)
            return;
        if (const auto* bin = dynamic_cast<const BinaryOpNode*>(expr))
        {
            visitExpr(bin->left());
            visitExpr(bin->right());
            return;
        }
        if (const auto* un = dynamic_cast<const UnaryOpNode*>(expr))
        {
            visitExpr(un->operand());
            return;
        }
        if (const auto* id = dynamic_cast<const IDNode*>(expr))
        {
            recordUsage(id->symbolId());
            return;
        }
        if (const auto* field = dynamic_cast<const FieldAccessNode*>(expr))
        {
            recordFieldAccess(*field);
            return;
        }
        if (const auto* call = dynamic_cast<const FunctionCallNode*>(expr))
        {
            if (call->symbolId() != InvalidSymbolID)
                recordUsage(call->symbolId());
            for (const auto& arg : call->args())
            {
                if (arg)
                    visitExpr(arg.get());
            }
            return;
        }
        if (const auto* memberCall = dynamic_cast<const MemberFunctionCallNode*>(expr))
        {
            recordUsage(memberCall->baseSymbolId());
            for (const auto& arg : memberCall->args())
            {
                if (arg)
                    visitExpr(arg.get());
            }
            return;
        }
    }

    void recordUsage(SymbolID id)
    {
        if (id != InvalidSymbolID)
            ++usageCount[id];
    }

    void recordFieldAccess(const FieldAccessNode& access)
    {
        recordUsage(access.baseSymbolId());
    }

    std::unordered_map<SymbolID, std::size_t> usageCount;
    std::unordered_set<SymbolID> declaredSymbols;
};

template <typename StmtList>
bool pruneStatementList(StmtList& statements, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = false;
    auto writeIt = statements.begin();
    for (auto it = statements.begin(); it != statements.end(); ++it)
    {
        auto& stmt = *it;
        bool drop = false;
        if (stmt)
        {
            if (auto* decl = dynamic_cast<DeclNode*>(stmt.get()))
            {
                if (unusedIds.count(decl->symbolId()))
                    drop = true;
            }
        }
        if (drop)
        {
            removed = true;
        }
        else
        {
            if (writeIt != it)
                *writeIt = std::move(*it);
            ++writeIt;
        }
    }
    statements.erase(writeIt, statements.end());
    return removed;
}

bool pruneBlock(BlockNode& block, const std::unordered_set<SymbolID>& unusedIds);

bool pruneStmt(StmtNode& stmt, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = false;
    if (auto* func = dynamic_cast<FunctionNode*>(&stmt))
    {
        if (auto* body = func->body())
            removed |= pruneBlock(*body, unusedIds);
        return removed;
    }
    if (auto* ifNode = dynamic_cast<IfNode*>(&stmt))
    {
        if (auto* thenBlock = ifNode->thenBlock())
            removed |= pruneBlock(*thenBlock, unusedIds);
        if (auto* elseBlock = ifNode->elseBlock())
            removed |= pruneBlock(*elseBlock, unusedIds);
        return removed;
    }
    if (auto* structDecl = dynamic_cast<StructDeclNode*>(&stmt))
    {
        for (auto& fn : structDecl->functions())
        {
            if (fn && fn->body())
                removed |= pruneBlock(*fn->body(), unusedIds);
        }
        return removed;
    }
    if (auto* nestedBlock = dynamic_cast<BlockNode*>(&stmt))
    {
        removed |= pruneBlock(*nestedBlock, unusedIds);
        return removed;
    }
    return removed;
}

bool pruneBlock(BlockNode& block, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = pruneStatementList(block.statements(), unusedIds);
    for (const auto& stmt : block.statements())
    {
        if (stmt)
            removed |= pruneStmt(*stmt, unusedIds);
    }
    return removed;
}

bool pruneProgram(ProgramNode& program, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = pruneStatementList(program.statements(), unusedIds);
    for (const auto& stmt : program.statements())
    {
        if (stmt)
            removed |= pruneStmt(*stmt, unusedIds);
    }
    return removed;
}
}

bool UnusedSymbolCleaner::removeUnusedVariables(ProgramNode& program)
{
    bool changed = false;
    while (true)
    {
        UsageAnalyzer analyzer;
        analyzer.analyze(program);

        std::unordered_set<SymbolID> unusedIds;
        for (SymbolID id : analyzer.declarations())
        {
            if (!analyzer.usages().count(id))
                unusedIds.insert(id);
        }

        if (unusedIds.empty())
            break;

        pruneProgram(program, unusedIds);
        changed = true;
    }
    return changed;
}
