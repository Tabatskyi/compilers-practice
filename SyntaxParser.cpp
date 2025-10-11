#include "SyntaxParser.hpp"

SyntaxParser::SyntaxParser(TokenStream tokens): m_tokens(std::move(tokens)) {}

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
    StmtList statements;

    skipNewlines();

    while (!atEnd())
    {
        const Token *token = peek();
        if (!token)
            break;

        if (token->type == TokenType::Return)
            break;

        auto stmt = parseStmt();
        if (!stmt)
            return nullptr;

        statements.push_back(std::move(stmt));
        skipNewlines();
    }

    const Token *retToken = peek();
    if (!retToken || retToken->type != TokenType::Return)
    {
        addError("Expected return statement at end of program");
        return nullptr;
    }

    auto ret = parseReturn();
    if (!ret)
        return nullptr;

    skipNewlines();
    if (!atEnd())
        addError("Unexpected tokens after return statement");

    return std::make_unique<ProgramNode>(std::move(statements), std::move(ret));
}

std::unique_ptr<StmtNode> SyntaxParser::parseStmt()
{
    skipNewlines();

    const Token *token = peek();
    if (!token)
    {
        addError("Unexpected end of input while parsing statement");
        return nullptr;
    }

    switch (token->type)
    {
        case TokenType::I32:
            return parseDecl();

        case TokenType::Identifier:
            return parseAssign();

        default:
            addError("Unexpected token '" + token->lexeme + "' at start of statement");
            return nullptr;
    }
}

std::unique_ptr<ReturnNode> SyntaxParser::parseReturn()
{
    if (!expect(TokenType::Return, "Expected 'return'"))
        return nullptr;

    skipNewlines();
    auto expr = parseExpr();
    if (!expr)
        return nullptr;

    return std::make_unique<ReturnNode>(std::move(expr));
}

std::unique_ptr<DeclNode> SyntaxParser::parseDecl()
{
    if (!expect(TokenType::I32, "Expected 'i32' at start of declaration"))
        return nullptr;

    skipNewlines();

    bool isMutable = false;
    while (match(TokenType::Mut))
        isMutable = true;

    const Token *identTok = peek();
    if (!identTok || identTok->type != TokenType::Identifier)
    {
        addError("Expected identifier after type specifier");
        return nullptr;
    }
    std::string identifier = identTok->lexeme;
    eat();

    skipNewlines();

    std::unique_ptr<ExprNode> initializer;
    if (match(TokenType::BlockStart))
    {
        skipNewlines();
        initializer = parseExpr();
        if (!initializer)
            return nullptr;

        skipNewlines();
        if (!expect(TokenType::BlockEnd, "Expected '}' after initializer expression"))
            return nullptr;
    }

    return std::make_unique<DeclNode>(std::move(identifier), isMutable, std::move(initializer));
}

std::unique_ptr<AssignNode> SyntaxParser::parseAssign()
{
    const Token *identTok = peek();
    if (!identTok || identTok->type != TokenType::Identifier)
    {
        addError("Expected identifier at start of assignment");
        return nullptr;
    }

    std::string identifier = identTok->lexeme;
    eat();

    skipNewlines();

    if (!expect(TokenType::Assign, "Expected '=' in assignment"))
        return nullptr;

    skipNewlines();

    auto value = parseExpr();
    if (!value)
        return nullptr;

    return std::make_unique<AssignNode>(std::move(identifier), std::move(value));
}

std::unique_ptr<ExprNode> SyntaxParser::parseExpr()
{
    auto first = parseFactor();
    if (!first)
        return nullptr;

    std::unique_ptr<ExprNode> expr = std::move(first);
    auto combined = parseBinaryTail(std::move(expr));
    return combined ? std::move(combined) : nullptr;
}

std::unique_ptr<FactorNode> SyntaxParser::parseFactor()
{
    const Token *token = peek();
    if (!token)
    {
        addError("Unexpected end of input while parsing factor");
        return nullptr;
    }

    switch (token->type)
    {
        case TokenType::Identifier:
        {
            std::string name = token->lexeme;
            eat();
            return std::make_unique<IDNode>(std::move(name));
        }

        case TokenType::Number:
        {
            int value = std::stoi(token->lexeme);
            eat();
            return std::make_unique<NumberNode>(value);
        }

        default:
            addError("Unexpected token '" + token->lexeme + "' in expression");
            return nullptr;
    }
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

void SyntaxParser::skipNewlines()
{
    while (match(TokenType::Newline)) {}
}

std::unique_ptr<ExprNode> SyntaxParser::parseBinaryTail(std::unique_ptr<ExprNode> left)
{
    if (!left)
        return nullptr;

    while (const Token *token = peek())
    {
        BinaryOpNode::Operator op;
        switch (token->type)
        {
            case TokenType::Add:
                op = BinaryOpNode::Operator::Add;
                break;
            case TokenType::Sub:
                op = BinaryOpNode::Operator::Sub;
                break;
            case TokenType::Mul:
                op = BinaryOpNode::Operator::Mul;
                break;
            default:
                return left;
        }

        eat();
        skipNewlines();

        auto rightFactor = parseFactor();
        if (!rightFactor)
            return nullptr;

        std::unique_ptr<ExprNode> right = std::move(rightFactor);
        left = std::make_unique<BinaryOpNode>(op, std::move(left), std::move(right));
        skipNewlines();
    }

    return left;
}

void SyntaxParser::addError(const std::string &message)
{
    m_errors.push_back(message);
}
