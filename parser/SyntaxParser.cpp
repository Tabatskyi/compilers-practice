#include "SyntaxParser.hpp"

SyntaxParser::SyntaxParser(std::vector<Token> tokens)
    : tokens(std::move(tokens))
{
    if (!this->tokens.empty())
        lastLine = this->tokens.front().line;
}

const Token* SyntaxParser::peek(size_t offset) const
{
    size_t target = index + offset;
    if (target >= tokens.size())
        return nullptr;
    return& tokens[target];
}

const Token* SyntaxParser::eat()
{
    if (atEnd())
        return nullptr;
    const Token* tok = &tokens[index++];
    lastLine = tok->line;
    return tok;
}

std::unique_ptr<ProgramNode> SyntaxParser::parseProgram()
{
    ProgramNode::StmtList statements;

    skipNewlines();

    while (peek() && peek()->type == TokenType::Struct)
    {
        auto sd = parseStructDecl();
        if (!sd) return nullptr;
        statements.push_back(std::move(sd));
        skipNewlines();
    }

    while (peek() && peek()->type == TokenType::Fn)
    {
        auto fn = parseFunction();
        if (!fn) return nullptr;
        statements.push_back(std::move(fn));
        skipNewlines();
    }

    while (!atEnd())
    {
        auto stmt = parseStmt();
        if (!stmt) return nullptr;
        statements.push_back(std::move(stmt));
        skipNewlines();
    }

    if (statements.empty())
    {
        addError("Program must end with a return statement");
        return nullptr;
    }
    if (dynamic_cast<ReturnNode*>(statements.back().get()) == nullptr)
    {
        addError("Program must end with a return statement");
        return nullptr;
    }

    return std::make_unique<ProgramNode>(std::move(statements), 0);
}

std::unique_ptr<StmtNode> SyntaxParser::parseStmt()
{
    skipNewlines();

    const Token* token = peek();
    if (!token)
    {
        addError("Unexpected end of input while parsing statement");
        return nullptr;
    }

    switch (token->type)
    {
        case TokenType::Struct:
            return parseStructDecl();
        case TokenType::Fn:
            return parseFunction();
        case TokenType::I32:
        case TokenType::I64:
        case TokenType::Bool:
            return parseDecl();

        case TokenType::Identifier:
        {
            std::string name = token->lexeme;
            for (const auto& s : knownStructs)
            {
                if (s == name)
                    return parseDecl();
            }
            return parseAssign();
        }

        case TokenType::Return:
            return parseReturn();

        case TokenType::If:
            return parseIf();

        default:
            addError("Unexpected token '" + token->lexeme + "' at start of statement");
            return nullptr;
    }
}

std::unique_ptr<ReturnNode> SyntaxParser::parseReturn()
{
    std::size_t line = currentLine();
    if (!expect(TokenType::Return, "Expected 'return'"))
        return nullptr;

    skipNewlines();
    auto expr = parseExpr();
    if (!expr)
        return nullptr;

    auto node = std::make_unique<ReturnNode>(std::move(expr));
    node->setLine(line);
    return node;
}

std::unique_ptr<IfNode> SyntaxParser::parseIf()
{
    std::size_t line = currentLine();
    if (!expect(TokenType::If, "Expected 'if'"))
        return nullptr;

    skipNewlines();
    auto condition = parseExpr();
    if (!condition)
        return nullptr;

    skipNewlines();
    auto thenBlock = parseBlock(allocateScopeId());
    if (!thenBlock)
        return nullptr;

    skipNewlines();
    std::unique_ptr<BlockNode> elseBlock;
    if (match(TokenType::Else))
    {
        skipNewlines();
        elseBlock = parseBlock(allocateScopeId());
        if (!elseBlock)
            return nullptr;
        skipNewlines();
    }

    auto node = std::make_unique<IfNode>(std::move(condition), std::move(thenBlock), std::move(elseBlock));
    node->setLine(line);
    return node;
}

std::unique_ptr<BlockNode> SyntaxParser::parseBlock(size_t scopeId)
{
    std::size_t line = currentLine();
    if (!expect(TokenType::BlockStart, "Expected '{' to start block"))
        return nullptr;

    BlockNode::StmtList statements;
    skipNewlines();

    while (true)
    {
        const Token* token = peek();
        if (!token)
        {
            addError("Unexpected end of input inside block");
            return nullptr;
        }

        if (token->type == TokenType::BlockEnd)
        {
            eat();
            break;
        }

        auto stmt = parseStmt();
        if (!stmt)
            return nullptr;

        statements.push_back(std::move(stmt));
        skipNewlines();
    }

    auto node = std::make_unique<BlockNode>(std::move(statements), scopeId);
    node->setLine(line);
    return node;
}

std::unique_ptr<DeclNode> SyntaxParser::parseDecl()
{
    TypeDesc type = parseType();
    if (type.kind == TypeDesc::Kind::Builtin && type.builtin == ValueType::Invalid)
        return nullptr;

    skipNewlines();

    bool isMutable = false;
    while (match(TokenType::Mut))
    {
        isMutable = true;
        skipNewlines();
    }

    const Token* identTok = peek();
    if (!identTok || identTok->type != TokenType::Identifier)
    {
        addError("Expected identifier after type specifier");
        return nullptr;
    }
    std::string identifier = identTok->lexeme;
    std::size_t line = identTok->line;
    eat();

    skipNewlines();

    std::vector<std::unique_ptr<ExprNode>> initializers;
    if (match(TokenType::BlockStart))
    {
        skipNewlines();
        if (type.kind == TypeDesc::Kind::Struct)
        {
            if (peek() && peek()->type != TokenType::BlockEnd)
            {
                while (true)
                {
                    auto expr = parseExpr();
                    if (!expr)
                        return nullptr;
                    initializers.push_back(std::move(expr));
                    skipNewlines();
                    if (match(TokenType::Comma))
                    {
                        skipNewlines();
                        continue;
                    }
                    break;
                }
            }
        }
        else
        {
            auto expr = parseExpr();
            if (!expr)
                return nullptr;
            initializers.push_back(std::move(expr));
        }

        skipNewlines();
        if (!expect(TokenType::BlockEnd, "Expected '}' after initializer expression"))
            return nullptr;
    }
    auto node = std::make_unique<DeclNode>(type, std::move(identifier), isMutable, std::move(initializers));
    node->setLine(line);
    return node;
}

TypeDesc SyntaxParser::parseType()
{
    const Token* token = peek();
    if (!token)
    {
        addError("Expected type specifier");
        return TypeDesc::Builtin(ValueType::Invalid);
    }

    switch (token->type)
    {
        case TokenType::I32:
            eat();
            return TypeDesc::Builtin(ValueType::I32);

        case TokenType::I64:
            eat();
            return TypeDesc::Builtin(ValueType::I64);

        case TokenType::Bool:
            eat();
            return TypeDesc::Builtin(ValueType::Bool);

        case TokenType::Identifier:
        {
            std::string name = token->lexeme;
            for (const auto& s : knownStructs)
            {
                if (s == name)
                {
                    eat();
                    return TypeDesc::Struct(name);
                }
            }
            addError("Unknown type '" + token->lexeme + "'");
            return TypeDesc::Builtin(ValueType::Invalid);
        }

        default:
            addError("Expected type specifier");
            return TypeDesc::Builtin(ValueType::Invalid);
    }
}

std::unique_ptr<StmtNode> SyntaxParser::parseAssign()
{
    const Token* identTok = peek();
    if (!identTok || identTok->type != TokenType::Identifier)
    {
        addError("Expected identifier at start of assignment");
        return nullptr;
    }

    std::string identifier = identTok->lexeme;
    std::size_t line = identTok->line;
    eat();

    std::vector<std::string> chain;
    skipNewlines();
    while (match(TokenType::Dot))
    {
        const Token* f = peek();
        if (!f || f->type != TokenType::Identifier)
        {
            addError("Expected field name after '.'");
            return nullptr;
        }
        chain.push_back(f->lexeme);
        eat();
        skipNewlines();
    }

    if (!expect(TokenType::Assign, "Expected '=' in assignment"))
        return nullptr;

    skipNewlines();

    auto value = parseExpr();
    if (!value)
        return nullptr;

    if (!chain.empty())
    {
        auto target = std::make_unique<FieldAccessNode>(std::move(identifier), std::move(chain));
        target->setLine(line);
        auto assign = std::make_unique<AssignFieldNode>(std::move(target), std::move(value));
        assign->setLine(line);
        return assign;
    }

    auto node = std::make_unique<AssignNode>(std::move(identifier), std::move(value));
    node->setLine(line);
    return node;
}

std::unique_ptr<ExprNode> SyntaxParser::parseExpr()
{
    return parseEquality();
}

std::unique_ptr<ExprNode> SyntaxParser::parseEquality()
{
    auto left = parseAdditive();
    if (!left)
        return nullptr;

    while (true)
    {
        if (match(TokenType::Equals))
        {
            skipNewlines();
            auto right = parseAdditive();
            if (!right)
                return nullptr;
            auto node = std::make_unique<BinaryOpNode>(BinaryOpNode::Operator::Equal, std::move(left), std::move(right));
            node->setLine(previousLine());
            left = std::move(node);
            skipNewlines();
            continue;
        }

        if (match(TokenType::NotEqual))
        {
            skipNewlines();
            auto right = parseAdditive();
            if (!right)
                return nullptr;
            auto node = std::make_unique<BinaryOpNode>(BinaryOpNode::Operator::NotEqual, std::move(left), std::move(right));
            node->setLine(previousLine());
            left = std::move(node);
            skipNewlines();
            continue;
        }

        break;
    }

    return left;
}

std::unique_ptr<ExprNode> SyntaxParser::parseAdditive()
{
    auto left = parseMultiplicative();
    if (!left)
        return nullptr;

    while (true)
    {
        if (match(TokenType::Add))
        {
            skipNewlines();
            auto right = parseMultiplicative();
            if (!right)
                return nullptr;
            auto node = std::make_unique<BinaryOpNode>(BinaryOpNode::Operator::Add, std::move(left), std::move(right));
            node->setLine(previousLine());
            left = std::move(node);
            skipNewlines();
            continue;
        }

        if (match(TokenType::Sub))
        {
            skipNewlines();
            auto right = parseMultiplicative();
            if (!right)
                return nullptr;
            auto node = std::make_unique<BinaryOpNode>(BinaryOpNode::Operator::Sub, std::move(left), std::move(right));
            node->setLine(previousLine());
            left = std::move(node);
            skipNewlines();
            continue;
        }

        break;
    }

    return left;
}

std::unique_ptr<ExprNode> SyntaxParser::parseMultiplicative()
{
    auto left = parseUnary();
    if (!left)
        return nullptr;

    while (match(TokenType::Mul))
    {
        skipNewlines();
        auto right = parseUnary();
        if (!right)
            return nullptr;
        auto node = std::make_unique<BinaryOpNode>(BinaryOpNode::Operator::Mul, std::move(left), std::move(right));
        node->setLine(previousLine());
        left = std::move(node);
        skipNewlines();
    }

    return left;
}

std::unique_ptr<ExprNode> SyntaxParser::parseUnary()
{
    if (match(TokenType::Not))
    {
        skipNewlines();
        auto operand = parseUnary();
        if (!operand)
            return nullptr;
        auto node = std::make_unique<UnaryOpNode>(UnaryOpNode::Operator::LogicalNot, std::move(operand));
        node->setLine(previousLine());
        return node;
    }

    return parsePrimary();
}

std::unique_ptr<ExprNode> SyntaxParser::parsePrimary()
{
    const Token* token = peek();
    if (!token)
    {
        addError("Unexpected end of input while parsing expression");
        return nullptr;
    }

    switch (token->type)
    {
        case TokenType::Identifier:
        {
            std::string name = token->lexeme;
            eat();

            if (match(TokenType::LParen))
            {
                std::vector<std::unique_ptr<ExprNode>> args;
                skipNewlines();
                if (peek() && peek()->type != TokenType::RParen)
                {
                    while (true)
                    {
                        auto arg = parseExpr();
                        if (!arg) return nullptr;
                        args.push_back(std::move(arg));
                        skipNewlines();
                        if (match(TokenType::Comma)) { skipNewlines(); continue; }
                        break;
                    }
                }
                if (!expect(TokenType::RParen, "Expected ')' after arguments"))
                    return nullptr;
                return std::make_unique<FunctionCallNode>(std::move(name), std::move(args));
            }

            if (peek() && peek()->type == TokenType::Dot)
            {
                std::vector<std::string> chain;
                while (match(TokenType::Dot))
                {
                    const Token* f = peek();
                    if (!f || f->type != TokenType::Identifier)
                    {
                        addError("Expected field name after '.'");
                        return nullptr;
                    }
                    chain.push_back(f->lexeme);
                    eat();
                }

                if (!chain.empty() && peek() && peek()->type == TokenType::LParen)
                {
                    std::string fnName = chain.back();
                    chain.pop_back();
                    if (!expect(TokenType::LParen, "Expected '(' after member function name"))
                        return nullptr;
                    std::vector<std::unique_ptr<ExprNode>> args;
                    skipNewlines();
                    if (peek() && peek()->type != TokenType::RParen)
                    {
                        while (true)
                        {
                            auto arg = parseExpr();
                            if (!arg) return nullptr;
                            args.push_back(std::move(arg));
                            skipNewlines();
                            if (match(TokenType::Comma)) { skipNewlines(); continue; }
                            break;
                        }
                    }
                    if (!expect(TokenType::RParen, "Expected ')' after arguments"))
                        return nullptr;
                    return std::make_unique<MemberFunctionCallNode>(std::move(name), std::move(chain), std::move(fnName), std::move(args));
                }

                if (peek() && peek()->type == TokenType::LParen)
                {
                    if (!chain.empty())
                    {
                        addError("Callable object syntax is only supported on plain variables, not on nested fields");
                        return nullptr;
                    }

                    if (!expect(TokenType::LParen, "Expected '(' after callable object base"))
                        return nullptr;

                    std::vector<std::unique_ptr<ExprNode>> args;
                    skipNewlines();
                    if (peek() && peek()->type != TokenType::RParen)
                    {
                        while (true)
                        {
                            auto arg = parseExpr();
                            if (!arg) return nullptr;
                            args.push_back(std::move(arg));
                            skipNewlines();
                            if (match(TokenType::Comma)) { skipNewlines(); continue; }
                            break;
                        }
                    }
                    if (!expect(TokenType::RParen, "Expected ')' after arguments"))
                        return nullptr;

                    std::vector<std::string> callChain;
                    callChain.push_back("call");
                    std::string fnName = callChain.back();
                    callChain.pop_back();
                    return std::make_unique<MemberFunctionCallNode>(std::move(name), std::move(callChain), std::move(fnName), std::move(args));
                }

                return std::make_unique<FieldAccessNode>(std::move(name), std::move(chain));
            }

            return std::make_unique<IDNode>(std::move(name));
        }
        case TokenType::Number:
        {
            std::int64_t value = std::stoll(token->lexeme);
            eat();
            return std::make_unique<NumberNode>(value);
        }
        case TokenType::True:
        case TokenType::False:
        {
            bool value = (token->type == TokenType::True);
            eat();
            return std::make_unique<BoolLiteralNode>(value);
        }
        default:
            addError("Unexpected token '" + token->lexeme + "' in expression");
            return nullptr;
    }
}

std::unique_ptr<StructDeclNode> SyntaxParser::parseStructDecl()
{
    if (!expect(TokenType::Struct, "Expected 'struct'"))
        return nullptr;

    const Token* nameTok = peek();
    if (!nameTok || nameTok->type != TokenType::Identifier)
    {
        addError("Expected struct name after 'struct'");
        return nullptr;
    }
    std::string structName = nameTok->lexeme;
    eat();

    skipNewlines();
    if (!expect(TokenType::BlockStart, "Expected '{' to start struct body"))
        return nullptr;

    knownStructs.push_back(structName);

    std::vector<StructDeclNode::Field> fields;
    std::vector<std::unique_ptr<FunctionNode>> methods;
    skipNewlines();
    bool parsingFields = true;
    while (true)
    {
        const Token* t = peek();
        if (!t)
        {
            addError("Unexpected end of input inside struct body");
            return nullptr;
        }
        if (t->type == TokenType::BlockEnd)
        {
            eat();
            break;
        }
        if (t->type == TokenType::Fn)
        {
            parsingFields = false;
            auto m = parseFunction(true, structName);
            if (!m) return nullptr;
            methods.push_back(std::move(m));
            skipNewlines();
            continue;
        }
        if (!parsingFields)
        {
            addError("Member functions must follow all fields");
            return nullptr;
        }
        TypeDesc fieldType = parseType();
        if (fieldType.kind == TypeDesc::Kind::Builtin && fieldType.builtin == ValueType::Invalid)
            return nullptr;
        skipNewlines();
        bool isMutable = false;
        while (match(TokenType::Mut))
        {
            isMutable = true;
            skipNewlines();
        }
        const Token* fieldNameTok = peek();
        if (!fieldNameTok || fieldNameTok->type != TokenType::Identifier)
        {
            addError("Expected field name in struct body");
            return nullptr;
        }
        std::string fieldName = fieldNameTok->lexeme;
        eat();
        skipNewlines();
        
        fields.push_back(StructDeclNode::Field{std::move(fieldType), std::move(fieldName), isMutable});
    }
    return std::make_unique<StructDeclNode>(std::move(structName), std::move(fields), std::move(methods));
}

std::unique_ptr<FunctionNode> SyntaxParser::parseFunction(bool isMember, const std::string& masterStruct)
{
    if (!expect(TokenType::Fn, "Expected 'fn'"))
        return nullptr;

    const Token* nameTok = peek();
    if (!nameTok || nameTok->type != TokenType::Identifier)
    {
        addError("Expected function name after 'fn'");
        return nullptr;
    }
    std::string funcName = nameTok->lexeme;
    eat();

    if (!expect(TokenType::Assign, "Expected '=' after function name"))
        return nullptr;

    if (!expect(TokenType::LParen, "Expected '(' after '=' in function declaration"))
        return nullptr;

    std::vector<FunctionNode::Param> params;
    if (isMember)
    {
        params.push_back(FunctionNode::Param{TypeDesc::Struct(masterStruct), std::string("_self"), InvalidSymbolID});
    }
    skipNewlines();
    if (peek() && peek()->type != TokenType::RParen)
    {
        while (true)
        {
            TypeDesc pType = parseType();
            if (pType.kind == TypeDesc::Kind::Builtin && pType.builtin == ValueType::Invalid)
                return nullptr;
            skipNewlines();

            const Token* paramNameTok = peek();
            if (!paramNameTok || paramNameTok->type != TokenType::Identifier)
            {
                addError("Expected parameter name");
                return nullptr;
            }
            std::string pName = paramNameTok->lexeme;
            eat();

            params.push_back(FunctionNode::Param{std::move(pType), std::move(pName), InvalidSymbolID});

            skipNewlines();
            if (match(TokenType::Comma))
            {
                skipNewlines();
                continue;
            }
            break;
        }
    }

    if (!expect(TokenType::RParen, "Expected ')' after parameter list"))
        return nullptr;

    if (!expect(TokenType::Arrow, "Expected '->' after parameter list"))
        return nullptr;

    TypeDesc retType = parseType();
    if (retType.kind == TypeDesc::Kind::Builtin && retType.builtin == ValueType::Invalid)
        return nullptr;

    skipNewlines();
    auto body = parseBlock(allocateScopeId());
    if (!body)
        return nullptr;

    if (isMember)
        return std::make_unique<FunctionNode>(std::move(funcName), std::move(params), std::move(retType), std::move(body), body->scopeId(), masterStruct);
    return std::make_unique<FunctionNode>(std::move(funcName), std::move(params), std::move(retType), std::move(body), body->scopeId());
}

bool SyntaxParser::atEnd() const
{
    return index >= tokens.size() || (tokens[index].type == TokenType::EndOfFile);
}

bool SyntaxParser::match(TokenType type)
{
    const Token* token = peek();
    if (token && token->type == type)
    {
        ++index;
        lastLine = token->line;
        return true;
    }
    return false;
}

bool SyntaxParser::expect(TokenType type, const std::string& message)
{
    if (match(type))
        return true;

    addError(message);
    return false;
}

std::size_t SyntaxParser::currentLine() const
{
    const Token* tok = peek();
    if (tok)
        return tok->line;
    return lastLine;
}

std::size_t SyntaxParser::previousLine() const
{
    return lastLine;
}

void SyntaxParser::skipNewlines()
{
    while (match(TokenType::Newline)) {}
}

size_t SyntaxParser::allocateScopeId()
{
    return nextScopeId++;
}

void SyntaxParser::addError(const std::string& message, std::size_t explicitLine)
{
    std::size_t line = explicitLine ? explicitLine : currentLine();
    errorList.push_back(Diagnostic{message, line});
}