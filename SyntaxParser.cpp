#include "SyntaxParser.hpp"

SyntaxParser::SyntaxParser(TokenStream tokens)
    : m_tokens(std::move(tokens))
{
}

const Token *SyntaxParser::peek(std::size_t offset) const
{
    std::size_t target = m_index + offset;
    if (target >= m_tokens.size())
        return nullptr;
    return &m_tokens[target];
}

const Token *SyntaxParser::eat()
{
    if (atEnd())
        return nullptr;
    return &m_tokens[m_index++];
}

std::unique_ptr<ProgramNode> SyntaxParser::parseProgram()
{
    return nullptr;
}

std::unique_ptr<StmtNode> SyntaxParser::parseStmt()
{
    return nullptr;
}

std::unique_ptr<ReturnNode> SyntaxParser::parseReturn()
{
    return nullptr;
}

std::unique_ptr<DeclNode> SyntaxParser::parseDecl()
{
    return nullptr;
}

std::unique_ptr<AssignNode> SyntaxParser::parseAssign()
{
    return nullptr;
}

std::unique_ptr<ExprNode> SyntaxParser::parseExpr()
{
    return nullptr;
}

std::unique_ptr<FactorNode> SyntaxParser::parseFactor()
{
    return nullptr;
}

bool SyntaxParser::atEnd() const
{
    return m_index >= m_tokens.size() || (m_tokens[m_index].type == TokenType::EndOfFile);
}

bool SyntaxParser::match(TokenType type)
{
    const Token *token = peek();
    if (token && token->type == type)
    {
        ++m_index;
        return true;
    }
    return false;
}

bool SyntaxParser::expect(TokenType type, const std::string &message)
{
    if (match(type))
        return true;

    addError(message);
    return false;
}

std::unique_ptr<ExprNode> SyntaxParser::parseBinaryTail(std::unique_ptr<ExprNode> left)
{
    (void)left;
    return nullptr;
}

void SyntaxParser::addError(const std::string &message)
{
    m_errors.push_back(message);
}
