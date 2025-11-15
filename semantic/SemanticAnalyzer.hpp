#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Semantic.hpp"
#include "../ast/AST.hpp"
#include "../ast/ASTVisitor.hpp"
#include "../general/Diagnostics.hpp"

class SemanticAnalyzer : public ASTVisitor
{
public:
    bool analyze(const ProgramNode& program);

    const std::vector<Diagnostic>& errors() const;
    const std::vector<Diagnostic>& warnings() const;
    const std::unordered_map<SymbolID, VariableInfo>& symbols() const;
    const StructTable& structs() const;
    const FunctionTable& functions() const;

    void visitProgram(const ProgramNode& node) override;
    void visitBlock(const BlockNode& node) override;
    void visitFunction(const FunctionNode& node) override;
    void visitDecl(const DeclNode& node) override;
    void visitAssign(const AssignNode& node) override;
    void visitIf(const IfNode& node) override;
    void visitReturn(const ReturnNode& node) override;
    void visitBinaryOp(const BinaryOpNode& node) override;
    void visitUnaryOp(const UnaryOpNode& node) override;
    void visitID(const IDNode& node) override;
    void visitNumber(const NumberNode& node) override;
    void visitBoolLiteral(const BoolLiteralNode& node) override;
    void visitFieldAccess(const FieldAccessNode& node) override;
    void visitAssignField(const AssignFieldNode& node) override;
    void visitMemberFunctionCall(const MemberFunctionCallNode& node) override;
    void visitFunctionCall(const FunctionCallNode& node) override;
    void visitStructDecl(const StructDeclNode& node) override;

private:
    void enterScope(size_t scopeId);
    void exitScope();
    size_t currentScopeId() const;
    SymbolID resolveSymbol(const std::string& name) const;

    void addError(const std::string& message, std::size_t line = 0);
    void addError(const std::string& message, const ASTNode& node);
    void addError(const std::string& message, const ASTNode* node);

    void addWarning(const std::string& message, std::size_t line = 0);
    void addWarning(const std::string& message, const ASTNode& node);
    void addWarning(const std::string& message, const ASTNode* node);

    void validateCallArguments(const std::vector<std::unique_ptr<ExprNode>>& args,
                               const FunctionInfo& funcInfo,
                               size_t paramStartIndex,
                               const std::string& undeclaredVarMessage);

    const FunctionInfo* findMemberFunction(const std::string& funcName, const std::string& structName) const;

    std::unordered_map<SymbolID, VariableInfo> symbolTable;
    std::unordered_map<size_t, std::unordered_map<std::string, SymbolID>> scopeSymbols;
    std::vector<size_t> scopeStack;
    SymbolID nextSymbolId = 0;
    std::vector<Diagnostic> errorList;
    std::vector<Diagnostic> warningList;
    bool returnSeen = false;
    StructTable structTable;
    FunctionTable functionTable;
    bool inFunction = false;
    TypeDesc currentFunctionReturn{TypeDesc::Builtin(ValueType::Invalid)};
    std::string currentMemberMaster;
};