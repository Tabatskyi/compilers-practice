#include "UsageAnalyzer.hpp"

void UsageAnalyzer::visitProgram(const ProgramNode& program)
{
    for (const std::unique_ptr<StmtNode>& stmt : program.statements())
    {
        if (stmt)
            visitStmt(*stmt);
    }
}

void UsageAnalyzer::visitBlock(const BlockNode& block)
{
    for (const std::unique_ptr<StmtNode>& stmt : block.statements())
    {
        if (stmt)
            visitStmt(*stmt);
    }
}

void UsageAnalyzer::visitStruct(const StructDeclNode& node)
{
    for (const std::unique_ptr<FunctionNode>& fn : node.functions())
    {
        if (fn && fn->body())
            visitBlock(*fn->body());
    }
}

void UsageAnalyzer::visitFunction(const FunctionNode& node)
{
    if (node.body())
        visitBlock(*node.body());
}

void UsageAnalyzer::visitStmt(const StmtNode& stmt)
{
    if (const DeclNode* decl = dynamic_cast<const DeclNode*>(&stmt))
    {
        SymbolID id = decl->symbolId();
        if (id != InvalidSymbolID)
            declaredSymbols.insert(id);
        for (const std::unique_ptr<ExprNode>& init : decl->initializers())
        {
            if (init)
                visitExpr(init.get());
        }
        return;
    }
    if (const AssignNode* assign = dynamic_cast<const AssignNode*>(&stmt))
    {
        recordUsage(assign->symbolId());
        if (assign->value())
            visitExpr(assign->value());
        return;
    }
    if (const AssignFieldNode* assignField = dynamic_cast<const AssignFieldNode*>(&stmt))
    {
        if (const FieldAccessNode* target = assignField->target())
            recordFieldAccess(*target);
        if (assignField->value())
            visitExpr(assignField->value());
        return;
    }
    if (const IfNode* ifNode = dynamic_cast<const IfNode*>(&stmt))
    {
        if (ifNode->condition())
            visitExpr(ifNode->condition());
        if (ifNode->thenBlock())
            visitBlock(*ifNode->thenBlock());
        if (ifNode->elseBlock())
            visitBlock(*ifNode->elseBlock());
        return;
    }
    if (const ReturnNode* ret = dynamic_cast<const ReturnNode*>(&stmt))
    {
        visitExpr(ret->expr());
        return;
    }
    if (const FunctionNode* func = dynamic_cast<const FunctionNode*>(&stmt))
    {
        visitFunction(*func);
        return;
    }
    if (const StructDeclNode* structDecl = dynamic_cast<const StructDeclNode*>(&stmt))
    {
        visitStruct(*structDecl);
        return;
    }
    if (const BlockNode* block = dynamic_cast<const BlockNode*>(&stmt))
    {
        visitBlock(*block);
        return;
    }
}

void UsageAnalyzer::visitExpr(const ExprNode* expr)
{
    if (!expr)
        return;
    if (const BinaryOpNode* bin = dynamic_cast<const BinaryOpNode*>(expr))
    {
        visitExpr(bin->left());
        visitExpr(bin->right());
        return;
    }
    if (const UnaryOpNode* un = dynamic_cast<const UnaryOpNode*>(expr))
    {
        visitExpr(un->operand());
        return;
    }
    if (const IDNode* id = dynamic_cast<const IDNode*>(expr))
    {
        recordUsage(id->symbolId());
        return;
    }
    if (const FieldAccessNode* field = dynamic_cast<const FieldAccessNode*>(expr))
    {
        recordFieldAccess(*field);
        return;
    }
    if (const FunctionCallNode* call = dynamic_cast<const FunctionCallNode*>(expr))
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
    if (const MemberFunctionCallNode* memberCall = dynamic_cast<const MemberFunctionCallNode*>(expr))
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

void UsageAnalyzer::recordUsage(SymbolID id)
{
    if (id != InvalidSymbolID)
        ++usageCount[id];
}

void UsageAnalyzer::recordFieldAccess(const FieldAccessNode& access)
{
    recordUsage(access.baseSymbolId());
}

template <typename StmtList>
bool UsageAnalyzer::pruneStatementList(StmtList& statements, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = false;
    auto writeIt = statements.begin();
    for (auto it = statements.begin(); it != statements.end(); ++it)
    {
        auto& stmt = *it;
        bool drop = false;
        if (stmt)
        {
            if (DeclNode* decl = dynamic_cast<DeclNode*>(stmt.get()))
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

bool UsageAnalyzer::pruneStmt(StmtNode& stmt, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = false;
    if (FunctionNode* func = dynamic_cast<FunctionNode*>(&stmt))
    {
        if (BlockNode* body = func->body())
            removed |= pruneBlock(*body, unusedIds);
        return removed;
    }
    if (IfNode* ifNode = dynamic_cast<IfNode*>(&stmt))
    {
        if (BlockNode* thenBlock = ifNode->thenBlock())
            removed |= pruneBlock(*thenBlock, unusedIds);
        if (BlockNode* elseBlock = ifNode->elseBlock())
            removed |= pruneBlock(*elseBlock, unusedIds);
        return removed;
    }
    if (StructDeclNode* structDecl = dynamic_cast<StructDeclNode*>(&stmt))
    {
        for (std::unique_ptr<FunctionNode>& fn : structDecl->functions())
        {
            if (fn && fn->body())
                removed |= pruneBlock(*fn->body(), unusedIds);
        }
        return removed;
    }
    if (BlockNode* nestedBlock = dynamic_cast<BlockNode*>(&stmt))
    {
        removed |= pruneBlock(*nestedBlock, unusedIds);
        return removed;
    }
    return removed;
}

bool UsageAnalyzer::pruneBlock(BlockNode& block, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = pruneStatementList(block.statements(), unusedIds);
    for (const std::unique_ptr<StmtNode>& stmt : block.statements())
    {
        if (stmt)
            removed |= pruneStmt(*stmt, unusedIds);
    }
    return removed;
}

bool UsageAnalyzer::pruneProgram(ProgramNode& program, const std::unordered_set<SymbolID>& unusedIds)
{
    bool removed = pruneStatementList(program.statements(), unusedIds);
    for (const std::unique_ptr<StmtNode>& stmt : program.statements())
    {
        if (stmt)
            removed |= pruneStmt(*stmt, unusedIds);
    }
    return removed;
}