#pragma once

#include "ASTNode.hpp"
#include "Token.hpp"

/// Syntax parser that consumes a token stream and produces an AST.
class SyntaxParser
{
public:
    explicit SyntaxParser(std::vector<Token> tokens);

    /// Peek at the token `offset` positions ahead. Returns nullptr when out of bounds.
    const Token* peek(size_t offset = 0) const;

    /// Consume and return the current token. Returns nullptr if already at end.
    const Token* eat();

    /// Parse the whole program according to the grammar.
    std::unique_ptr<ProgramNode> parseProgram();

    /// Parse an individual statement (declaration or assignment).
    std::unique_ptr<StmtNode> parseStmt();

    /// Parse a return statement.
    std::unique_ptr<ReturnNode> parseReturn();
    std::unique_ptr<IfNode> parseIf();
    std::unique_ptr<BlockNode> parseBlock(size_t scopeId);

    /// Parse either a declaration or assignment depending on current token.
    std::unique_ptr<DeclNode> parseDecl();
    std::unique_ptr<StmtNode> parseAssign();
    std::unique_ptr<StructDeclNode> parseStructDecl();
    std::unique_ptr<FunctionNode> parseFunction(bool isMember = false, const std::string& ownerStruct = "");

    /// Parse expressions and sub-components.
    std::unique_ptr<ExprNode> parseExpr();
    std::unique_ptr<ExprNode> parseEquality();
    std::unique_ptr<ExprNode> parseAdditive();
    std::unique_ptr<ExprNode> parseMultiplicative();
    std::unique_ptr<ExprNode> parseUnary();
    std::unique_ptr<ExprNode> parsePrimary();

    /// Whether parsing produced any errors.
    bool hasErrors() const { return !errorList.empty(); }

    /// Retrieve the list of diagnostic messages.
    const std::vector<std::string>& errors() const { return errorList; }

private:
    bool atEnd() const;
    bool match(TokenType type);
    bool expect(TokenType type, const std::string& message);

    void skipNewlines();
    TypeDesc parseType();
    size_t allocateScopeId();

    void addError(const std::string& message);

    std::vector<Token> tokens;
    size_t index = 0;
    std::vector<std::string> errorList;
    size_t nextScopeId = 1;
    std::vector<std::string> knownStructs;
};
