#include "Token.hpp"
#include "SyntaxParser.hpp"

#include <limits>
#include <sstream>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using std::string;

struct IRContext
{
    int tempId = 0;
    std::ostringstream ir;
};

struct VariableInfo
{
    TypeDesc type;
    bool isMutable;
    string name;
    size_t scopeId;
};

static const std::unordered_map<string, TokenType> keywordMap =
{
    {"var", TokenType::Var},
    {"struct", TokenType::Struct},
    {"fn", TokenType::Fn},
    {"mut", TokenType::Mut},
    {"return", TokenType::Return},
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"i32", TokenType::I32},
    {"i64", TokenType::I64},
    {"bool", TokenType::Bool},
    {"true", TokenType::True},
    {"false", TokenType::False}
};

TokenType classifyIdentifier(const string& ident)
{
    auto it = keywordMap.find(ident);
    return (it != keywordMap.end()) ? it->second : TokenType::Identifier;
}

std::vector<Token> lexSource(const string& source)
{
    std::vector<Token> out;
    enum class State { Start, Identifier, Number };
    State state = State::Start;
    string buffer;

    for (size_t i = 0; i <= source.size(); )
    {
        char c = (i < source.size()) ? source[i] : '\0';
        bool atEnd = (i == source.size());
        TokenType kind;

        switch (state)
        {
            case State::Start:
                if (atEnd)
                {
                    ++i;
                    continue;
                }
                if (c == '\n')
                {
                    out.push_back(Token{"\n", TokenType::Newline});
                    ++i;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(c)))
                {
                    ++i;
                    continue;
                }
                if (c == '/' && i + 1 < source.size() && source[i + 1] == '/')
                {
                    i += 2;
                    while (i < source.size() && source[i] != '\n') ++i;
                    continue;
                }
                if (c == '=' && i + 1 < source.size() && source[i + 1] == '=')
                {
                    out.push_back(Token{"==", TokenType::Equals});
                    i += 2;
                    continue;
                }
                if (c == '!' && i + 1 < source.size() && source[i + 1] == '=')
                {
                    out.push_back(Token{"!=", TokenType::NotEqual});
                    i += 2;
                    continue;
                }
                if (c == '!')
                {
                    out.push_back(Token{"!", TokenType::Not});
                    ++i;
                    continue;
                }
                if (c == '-' && i + 1 < source.size() && source[i + 1] == '>')
                {
                    out.push_back(Token{"->", TokenType::Arrow});
                    i += 2;
                    continue;
                }
                if (std::isalpha(static_cast<unsigned char>(c)))
                {
                    buffer.assign(1, c);
                    state = State::Identifier;
                    ++i;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c)))
                {
                    buffer.assign(1, c);
                    state = State::Number;
                    ++i;
                    continue;
                }

                kind = TokenType::Newline;
                switch (c)
                {
                    case '{': kind = TokenType::BlockStart; break;
                    case '}': kind = TokenType::BlockEnd; break;
                    case '(': kind = TokenType::LParen; break;
                    case ')': kind = TokenType::RParen; break;
                    case '.': kind = TokenType::Dot; break;
                    case ',': kind = TokenType::Comma; break;
                    case '=': kind = TokenType::Assign; break;
                    case '+': kind = TokenType::Add; break;
                    case '-': kind = TokenType::Sub; break;
                    case '*': kind = TokenType::Mul; break;
                    default: ++i; continue;
                }
                out.push_back(Token{string(1, c), kind});
                ++i;
                continue;

            case State::Identifier:
                if (!atEnd && std::isalnum(static_cast<unsigned char>(c)))
                {
                    buffer.push_back(c);
                    ++i;
                    continue;
                }
                out.push_back(Token{buffer, classifyIdentifier(buffer)});
                buffer.clear();
                state = State::Start;
                continue;

            case State::Number:
                if (!atEnd && std::isdigit(static_cast<unsigned char>(c)))
                {
                    buffer.push_back(c);
                    ++i;
                    continue;
                }
                out.push_back(Token{buffer, TokenType::Number});
                buffer.clear();
                state = State::Start;
                continue;
        }
    }

    out.push_back(Token{"", TokenType::EndOfFile});
    return out;
}

static string typeToString(ValueType type)
{
    switch (type)
    {
        case ValueType::I32: return "i32";
        case ValueType::I64: return "i64";
        case ValueType::Bool: return "bool";
        default: return "<invalid>";
    }
}

static bool isNumeric(ValueType type)
{
    return type == ValueType::I32 || type == ValueType::I64;
}

static ValueType widerType(ValueType lhs, ValueType rhs)
{
    if (!isNumeric(lhs) || !isNumeric(rhs))
        return ValueType::Invalid;
    if (lhs == ValueType::I64 || rhs == ValueType::I64)
        return ValueType::I64;
    return ValueType::I32;
}

static ValueType comparisonOperandType(ValueType lhs, ValueType rhs)
{
    if (lhs == ValueType::Bool && rhs == ValueType::Bool)
        return ValueType::Bool;
    return widerType(lhs, rhs);
}

static bool isAssignable(ValueType target, ValueType source)
{
    if (target == ValueType::Invalid || source == ValueType::Invalid)
        return false;
    if (target == source)
        return true;
    if (target == ValueType::I64 && source == ValueType::I32)
        return true;
    return false;
}

static bool canConvertToI32(ValueType type)
{
    return type == ValueType::I32 || type == ValueType::I64 || type == ValueType::Bool;
}

class SemanticAnalyzer : public ASTVisitor
{
public:
    bool analyze(const ProgramNode& program)
    {
        errorList.clear();
        warningList.clear();
        symbolTable.clear();
        scopeSymbols.clear();
        scopeStack.clear();
        nextSymbolId = 0;
        returnSeen = false;
        functionTable.clear();

        program.accept(*this);

        if (!returnSeen)
            addError("Program must end with a return statement");

        return errorList.empty();
    }

    const std::vector<string>& errors() const { return errorList; }
    const std::vector<string>& warnings() const { return warningList; }
    const std::unordered_map<SymbolID, VariableInfo>& symbols() const { return symbolTable; }
    struct StructFieldInfo 
    { 
        TypeDesc type; 
        bool isMutable; 
        string name; 
    };
    struct StructInfo 
    { 
        string name; 
        std::vector<StructFieldInfo> fields; 
    };
    struct FunctionInfo 
    {
        string name; 
        TypeDesc returnType; 
        struct Param 
        { 
            TypeDesc type; 
            string name; 
            SymbolID symbolId; 
        }; 
        std::vector<Param> params; 
        size_t scopeId = 0; 
        string masterStruct; 
        bool isMember = false;
    };
    const std::unordered_map<string, StructInfo>& structs() const { return structTable; }
    const std::unordered_map<string, FunctionInfo>& functions() const { return functionTable; }

    void visitProgram(const ProgramNode& node) override
    {
        enterScope(node.scopeId());
        for (const auto& stmt : node.statements())
        {
            if (stmt)
                stmt->accept(*this);
        }
        exitScope();
    }

    void visitBlock(const BlockNode& node) override
    {
        enterScope(node.scopeId());
        for (const auto& stmt : node.statements())
        {
            if (stmt)
                stmt->accept(*this);
        }
        exitScope();
    }

    void visitFunction(const FunctionNode& node) override
    {
        if (functionTable.count(node.name()))
        {
            addError("Function '" + node.name() + "' redeclared");
            return;
        }

        FunctionInfo info;
        info.name = node.name();
        info.returnType = node.returnType();
        info.scopeId = node.scopeId();
        info.isMember = node.isMember();
        info.masterStruct = node.isMember() ? node.masterStruct() : "";
        for (const auto& p : node.params())
            info.params.push_back(FunctionInfo::Param{p.type, p.name, InvalidSymbolID});
        functionTable.emplace(info.name, std::move(info));

        enterScope(node.scopeId());
        for (const auto& p : node.params())
        {
            auto& scopeMap = scopeSymbols[currentScopeId()];
            if (scopeMap.count(p.name))
            {
                addError("Parameter '" + p.name + "' redeclared");
                continue;
            }
            SymbolID symbolId = nextSymbolId++;
            scopeMap.emplace(p.name, symbolId);
            symbolTable.emplace(symbolId, VariableInfo{p.type, false, p.name, currentScopeId()});

            auto fit = functionTable.find(node.name());
            if (fit != functionTable.end())
            {
                for (auto& fp : fit->second.params)
                {
                    if (fp.name == p.name)
                    {
                        fp.symbolId = symbolId;
                        break;
                    }
                }
            }
        }

        std::string prevMaster = currentMemberMaster;
        inFunction = true;
        currentMemberMaster = node.isMember() ? node.masterStruct() : "";
        currentFunctionReturn = node.returnType();
        if (node.body())
            node.body()->accept(*this);
        inFunction = false;
        currentMemberMaster = prevMaster;
        exitScope();
    }

    void visitDecl(const DeclNode& node) override
    {
        const string& name = node.identifier();
        size_t scope = currentScopeId();
        auto& scopeMap = scopeSymbols[scope];
        if (scopeMap.count(name))
        {
            addError("Variable '" + name + "' redeclared");
            return;
        }

        if (node.declaredType().kind == TypeDesc::Kind::Builtin)
        {
            if (node.hasInitializer())
            {
                if (node.initializers().size() != 1)
                {
                    addError("Builtin variable '" + name + "' must have a single initializer expression");
                }
                else
                {
                    const ExprNode* init = node.initializers()[0].get();
                    init->accept(*this);
                    ValueType initType = init->type();
                    if (!isAssignable(node.declaredType().builtin, initType))
                    {
                        addError("Cannot initialize '" + name + "' of type " + typeToString(node.declaredType().builtin) +
                                 " with value of type " + typeToString(initType));
                    }
                }
            }
        }
        else
        {
            auto it = structTable.find(node.declaredType().structName);
            if (it == structTable.end())
            {
                addError("Unknown struct type '" + node.declaredType().structName + "'");
            }
            else if (node.hasInitializer())
            {
                const auto& fields = it->second.fields;
                if (node.initializers().size() != fields.size())
                {
                    addError("Initializer list for struct '" + name + "' has wrong number of elements");
                }
                size_t count = std::min(node.initializers().size(), fields.size());
                for (size_t i = 0; i < count; ++i)
                {
                    const auto& field = fields[i];
                    const ExprNode* expr = node.initializers()[i].get();
                    expr->accept(*this);
                    if (field.type.kind == TypeDesc::Kind::Builtin)
                    {
                        ValueType t = expr->type();
                        if (!isAssignable(field.type.builtin, t))
                        {
                            addError("Cannot initialize field '" + field.name + "' of struct '" + name + "' with incompatible type");
                        }
                    }
                    else
                    {
                        const IDNode* id = dynamic_cast<const IDNode*>(expr);
                        if (!id)
                        {
                            addError("Field '" + field.name + "' requires struct '" + field.type.structName + "' value");
                        }
                        else
                        {
                            SymbolID srcId = id->symbolId();
                            if (srcId == InvalidSymbolID)
                            {
                                addError("Use of undeclared variable '" + id->name() + "' in struct initializer");
                            }
                            else
                            {
                                const auto& srcVar = symbolTable[srcId];
                                if (srcVar.type.kind != TypeDesc::Kind::Struct || srcVar.type.structName != field.type.structName)
                                {
                                    addError("Struct field '" + field.name + "' type mismatch in initializer");
                                }
                            }
                        }
                    }
                }
            }
        }

        SymbolID symbolId = nextSymbolId++;
        scopeMap.emplace(name, symbolId);
        symbolTable.emplace(symbolId, VariableInfo{node.declaredType(), node.isMutable(), name, scope});
        node.setSymbolId(symbolId);
    }

    void visitAssign(const AssignNode& node) override
    {
        SymbolID symbolId = resolveSymbol(node.identifier());
        if (symbolId == InvalidSymbolID)
        {
            addError("Assignment to undeclared variable '" + node.identifier() + "'");
        }
        else
        {
            const auto& info = symbolTable[symbolId];
            if (!info.isMutable)
                addError("Variable '" + node.identifier() + "' is immutable");
            if (info.type.kind == TypeDesc::Kind::Struct)
                addError("Assignment to struct variables is not supported in this task");
            node.setSymbolId(symbolId);
        }

        if (const ExprNode* value = node.value())
        {
            value->accept(*this);
            if (symbolId != InvalidSymbolID)
            {
                const auto& info = symbolTable[symbolId];
                ValueType valueType = value->type();
                if (info.type.kind != TypeDesc::Kind::Builtin || !isAssignable(info.type.builtin, valueType))
                {
                    addError("Cannot assign value of type " + typeToString(valueType) +
                             " to variable '" + node.identifier() + "' of type " + (info.type.kind == TypeDesc::Kind::Builtin ? typeToString(info.type.builtin) : ("struct " + info.type.structName)));
                }
            }
        }
    }

    void visitIf(const IfNode& node) override
    {
        if (const ExprNode* cond = node.condition())
        {
            cond->accept(*this);
            if (cond->type() != ValueType::Bool)
                addError("Condition of if statement must be bool");
        }

        if (const BlockNode* thenBlock = node.thenBlock())
            thenBlock->accept(*this);
        if (const BlockNode* elseBlock = node.elseBlock())
            elseBlock->accept(*this);
    }

    void visitReturn(const ReturnNode& node) override
    {
        if (!node.expr())
        {
            addError("Return statement requires an expression");
            return;
        }
        node.expr()->accept(*this);
        if (inFunction)
        {
            if (currentFunctionReturn.kind == TypeDesc::Kind::Builtin)
            {
                ValueType t = node.expr()->type();
                if (!isAssignable(currentFunctionReturn.builtin, t))
                    addError("Return type mismatch");
            }
            else
            {
                const IDNode* id = dynamic_cast<const IDNode*>(node.expr());
                if (!id)
                    addError("Return of struct must be a variable");
                else
                {
                    SymbolID sid = id->symbolId();
                    if (sid == InvalidSymbolID)
                        addError("Return references undeclared variable");
                    else
                    {
                        const auto& v = symbolTable[sid];
                        if (v.type.kind != TypeDesc::Kind::Struct || v.type.structName != currentFunctionReturn.structName)
                            addError("Return struct type mismatch");
                    }
                }
            }
        }
        else
        {
            returnSeen = true;
            ValueType type = node.expr()->type();
            if (!canConvertToI32(type))
                addError("Return type must be convertible to i32, got " + typeToString(type));
        }
    }

    void visitBinaryOp(const BinaryOpNode& node) override
    {
        if (const ExprNode* left = node.left())
            left->accept(*this);
        if (const ExprNode* right = node.right())
            right->accept(*this);

        const ExprNode* left = node.left();
        const ExprNode* right = node.right();
        ValueType leftType = left ? left->type() : ValueType::Invalid;
        ValueType rightType = right ? right->type() : ValueType::Invalid;

        if (leftType == ValueType::Invalid || rightType == ValueType::Invalid)
        {
            node.setType(ValueType::Invalid);
            return;
        }

        switch (node.op())
        {
            case BinaryOpNode::Operator::Add:
            case BinaryOpNode::Operator::Sub:
            case BinaryOpNode::Operator::Mul:
            {
                if (!isNumeric(leftType) || !isNumeric(rightType))
                {
                    addError("Arithmetic operators require numeric operands");
                    node.setType(ValueType::Invalid);
                    return;
                }
                node.setType(widerType(leftType, rightType));
                return;
            }
            case BinaryOpNode::Operator::Equal:
            case BinaryOpNode::Operator::NotEqual:
            {
                ValueType operandType = comparisonOperandType(leftType, rightType);
                if (operandType == ValueType::Invalid)
                {
                    addError("Comparison requires compatible operand types");
                    node.setType(ValueType::Invalid);
                    return;
                }
                node.setType(ValueType::Bool);
                return;
            }
        }

        node.setType(ValueType::Invalid);
    }

    void visitUnaryOp(const UnaryOpNode& node) override
    {
        if (const ExprNode* operand = node.operand())
            operand->accept(*this);

        const ExprNode* operand = node.operand();
        ValueType operandType = operand ? operand->type() : ValueType::Invalid;
        if (operandType != ValueType::Bool)
        {
            addError("Logical not operator requires bool operand");
            node.setType(ValueType::Invalid);
            return;
        }

        node.setType(ValueType::Bool);
    }

    void visitID(const IDNode& node) override
    {
        SymbolID symbolId = resolveSymbol(node.name());
        if (symbolId == InvalidSymbolID)
        {
            if (inFunction && !currentMemberMaster.empty())
            {
                auto it = structTable.find(currentMemberMaster);
                if (it != structTable.end())
                {
                    for (const auto& field : it->second.fields)
                    {
                        if (field.name == node.name())
                        {
                            if (field.type.kind == TypeDesc::Kind::Builtin)
                            {
                                node.setType(field.type.builtin);
                                return;
                            }
                            node.setType(ValueType::Invalid);
                            return;
                        }
                    }
                }
            }
            addError("Use of undeclared variable '" + node.name() + "'");
            node.setType(ValueType::Invalid);
            return;
        }
        node.setSymbolId(symbolId);
    const auto& info = symbolTable[symbolId];
        if (info.type.kind == TypeDesc::Kind::Builtin)
            node.setType(info.type.builtin);
        else
            node.setType(ValueType::Invalid);
    }

    void visitNumber(const NumberNode& node) override
    {
        std::int64_t value = node.value();
        if (value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max())
            node.setType(ValueType::I32);
        else
            node.setType(ValueType::I64);
    }

    void visitBoolLiteral(const BoolLiteralNode& node) override
    {
        node.setType(ValueType::Bool);
    }

    void visitFieldAccess(const FieldAccessNode& node) override
    {
        SymbolID baseId = resolveSymbol(node.base());
        if (baseId == InvalidSymbolID)
        {
            addError("Use of undeclared variable '" + node.base() + "'");
            return;
        }
        node.setBaseSymbolId(baseId);
        const auto& baseVar = symbolTable[baseId];
        if (baseVar.type.kind != TypeDesc::Kind::Struct)
        {
            addError("Variable '" + node.base() + "' is not a struct");
            node.setType(ValueType::Invalid);
            return;
        }
        TypeDesc current = baseVar.type;
        for (size_t i = 0; i < node.fieldChain().size(); ++i)
        {
            auto it = structTable.find(current.structName);
            if (it == structTable.end())
            {
                addError("Unknown struct type '" + current.structName + "'");
                node.setType(ValueType::Invalid);
                return;
            }
            const auto& fields = it->second.fields;
            const std::string& fieldName = node.fieldChain()[i];
            bool found = false;
            for (const auto& field : fields)
            {
                if (field.name == fieldName)
                {
                    current = field.type;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                addError("Struct '" + it->second.name + "' has no field '" + fieldName + "'");
                node.setType(ValueType::Invalid);
                return;
            }
        }
        if (current.kind == TypeDesc::Kind::Builtin)
            node.setType(current.builtin);
        else
            node.setType(ValueType::Invalid);
    }

    void visitAssignField(const AssignFieldNode& node) override
    {
        const FieldAccessNode* fieldAccess = node.target();
        if (!fieldAccess)
            return;

        visitFieldAccess(*fieldAccess);
        SymbolID baseId = fieldAccess->baseSymbolId();
        if (baseId == InvalidSymbolID)
            return;
        const auto& baseVar = symbolTable[baseId];

        bool chainMutable = baseVar.isMutable;
        TypeDesc current = baseVar.type;
        for (size_t i = 0; i < fieldAccess->fieldChain().size(); ++i)
        {
            auto it = structTable.find(current.structName);
            if (it == structTable.end())
            {
                chainMutable = false; 
                break; 
            }
            const std::string& fieldName = fieldAccess->fieldChain()[i];
            bool found = false;
            for (const auto& field : it->second.fields)
            {
                if (field.name == fieldName)
                {
                    if (i < fieldAccess->fieldChain().size() - 1)
                        chainMutable = chainMutable && field.isMutable;
                    current = field.type;
                    found = true;
                    break;
                }
            }
            if (!found) 
            { 
                chainMutable = false; 
                break; 
            }
        }

        if (!chainMutable)
            addError("Field access is immutable");

        if (current.kind == TypeDesc::Kind::Struct)
        {
            addError("Assignment to struct fields is not supported in this task");
        }

        if (const ExprNode* value = node.value())
        {
            value->accept(*this);
            if (current.kind == TypeDesc::Kind::Builtin)
            {
                if (!isAssignable(current.builtin, value->type()))
                    addError("Cannot assign incompatible type to field");
            }
        }
    }

    void visitMemberFunctionCall(const MemberFunctionCallNode& node) override
    {
        SymbolID baseId = resolveSymbol(node.base());
        if (baseId == InvalidSymbolID)
        {
            addError("Use of undeclared variable '" + node.base() + "'");
            return;
        }
        node.setBaseSymbolId(baseId);
        const auto& baseVar = symbolTable[baseId];

        if (baseVar.type.kind != TypeDesc::Kind::Struct)
        {
            addError("Variable '" + node.base() + "' is not a struct");
            return;
        }

        TypeDesc current = baseVar.type;
        for (size_t i = 0; i < node.fieldChain().size(); ++i)
        {
            auto it = structTable.find(current.structName);
            if (it == structTable.end()) { addError("Unknown struct type '" + current.structName + "'"); return; }
            const string& fieldName = node.fieldChain()[i];
            bool found = false;
            for (const auto& field : it->second.fields)
            {
                if (field.name == fieldName)
                {
                    current = field.type;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                addError("Struct '" + it->second.name + "' has no field '" + fieldName + "'");
                return;
            }
        }
        if (current.kind != TypeDesc::Kind::Struct)
        {
            addError("Member function call base must resolve to a struct");
            return;
        }

        const FunctionInfo* funcInfo = findMemberFunction(node.funcName(), current.structName);
        if (!funcInfo)
        {
            addError("Call to undeclared member function '" + node.funcName() + "'");
            return;
        }

        if (funcInfo->params.empty())
        {
            addError("Corrupt member function info");
            return;
        }

        size_t expectedUserArgs = funcInfo->params.size() - 1;
        if (node.args().size() != expectedUserArgs)
        {
            addError("Member function '" + node.funcName() + "' called with wrong number of arguments");
            return;
        }

        validateCallArguments(node.args(), *funcInfo, 1, "Use of undeclared variable in member function call");

        if (funcInfo->returnType.kind == TypeDesc::Kind::Builtin)
            const_cast<MemberFunctionCallNode&>(node).setType(funcInfo->returnType.builtin);
        else
            const_cast<MemberFunctionCallNode&>(node).setType(ValueType::Invalid);
    }

    void visitFunctionCall(const FunctionCallNode& node) override
    {
        auto it = functionTable.find(node.name());
        if (it == functionTable.end())
        {
            addError("Call to undeclared function '" + node.name() + "'");
            return;
        }
        const auto& func = it->second;
        if (node.args().size() != func.params.size())
        {
            addError("Function '" + node.name() + "' called with wrong number of arguments");
            return;
        }
    validateCallArguments(node.args(), func, 0, "Use of undeclared variable in function call");
        if (func.returnType.kind == TypeDesc::Kind::Builtin)
        {
            const_cast<FunctionCallNode&>(node).setType(func.returnType.builtin);
        }
        else
        {
            const_cast<FunctionCallNode&>(node).setType(ValueType::Invalid);
        }
    }

private:
    void enterScope(size_t scopeId)
    {
    scopeStack.push_back(scopeId);
    scopeSymbols.try_emplace(scopeId, std::unordered_map<string, SymbolID>{});
    }

    void exitScope()
    {
        if (!scopeStack.empty())
            scopeStack.pop_back();
    }

    size_t currentScopeId() const
    {
        return scopeStack.empty() ? 0 : scopeStack.back();
    }

    SymbolID resolveSymbol(const string& name) const
    {
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it)
        {
            auto scopeIt = scopeSymbols.find(*it);
            if (scopeIt == scopeSymbols.end())
                continue;

            auto symIt = scopeIt->second.find(name);
            if (symIt != scopeIt->second.end())
                return symIt->second;
        }
        return InvalidSymbolID;
    }

    void addError(const string& message)
    {
        errorList.push_back(message);
    }

    void addWarning(const string& message)
    {
        warningList.push_back(message);
    }

    void validateCallArguments(const std::vector<std::unique_ptr<ExprNode>>& args, const FunctionInfo& funcInfo, size_t paramStartIndex, const string& undeclaredVarMessage)
    {
        for (size_t i = 0; i < args.size(); ++i)
        {
            const ExprNode* arg = args[i] ? args[i].get() : nullptr;
            if (!arg)
                continue;

            arg->accept(*this);
            if (paramStartIndex + i >= funcInfo.params.size())
            {
                addError("Argument type mismatch at position " + std::to_string(i));
                continue;
            }

            const auto& param = funcInfo.params[paramStartIndex + i];
            if (param.type.kind == TypeDesc::Kind::Builtin)
            {
                if (!isAssignable(param.type.builtin, arg->type()))
                    addError("Argument type mismatch at position " + std::to_string(i));
            }
            else
            {
                const IDNode* id = dynamic_cast<const IDNode*>(arg);
                if (!id)
                {
                    addError("Struct argument must be a variable of type '" + param.type.structName + "'");
                }
                else if (id->symbolId() == InvalidSymbolID)
                {
                    addError(undeclaredVarMessage);
                }
                else
                {
                    auto varIt = symbolTable.find(id->symbolId());
                    if (varIt == symbolTable.end())
                    {
                        addError(undeclaredVarMessage);
                    }
                    else
                    {
                        const auto& var = varIt->second;
                        if (var.type.kind != TypeDesc::Kind::Struct || var.type.structName != param.type.structName)
                            addError("Struct argument type mismatch");
                    }
                }
            }
        }
    }

    const FunctionInfo* findMemberFunction(const string& funcName, const string& structName) const
    {
        auto it = functionTable.find(funcName);
        if (it != functionTable.end() && it->second.isMember && it->second.masterStruct == structName)
            return &it->second;
        for (const auto& entry : functionTable)
        {
            if (entry.second.name == funcName && entry.second.isMember && entry.second.masterStruct == structName)
                return &entry.second;
        }
        return nullptr;
    }

    std::unordered_map<SymbolID, VariableInfo> symbolTable;
    std::unordered_map<size_t, std::unordered_map<string, SymbolID>> scopeSymbols;
    std::vector<size_t> scopeStack;
    SymbolID nextSymbolId = 0;
    std::vector<string> errorList;
    std::vector<string> warningList;
    bool returnSeen = false;
    std::unordered_map<string, StructInfo> structTable;
    std::unordered_map<string, FunctionInfo> functionTable;
    bool inFunction = false;
    TypeDesc currentFunctionReturn{TypeDesc::Builtin(ValueType::Invalid)};
    std::string currentMemberMaster;
public:
    void visitStructDecl(const StructDeclNode& node) override
    {
        if (structTable.count(node.name()))
        {
            addError("Struct '" + node.name() + "' redeclared");
            return;
        }

        StructInfo info;
        info.name = node.name();
        for (const StructDeclNode::Field& f : node.fields())
        {
            if (f.type.kind == TypeDesc::Kind::Struct)
            {
                if (!structTable.count(f.type.structName))
                {
                    addError("Unknown struct type '" + f.type.structName + "' used in struct '" + node.name() + "'");
                }
            }
            info.fields.push_back(StructFieldInfo{f.type, f.isMutable, f.name});
        }
        structTable.emplace(info.name, std::move(info));
        for (const std::unique_ptr<FunctionNode>& func : node.functions())
        {
            if (func)
                func->accept(*this);
        }
    }
};

struct CodegenValue
{
    string operand;
    bool isStruct = false;
    ValueType type = ValueType::Invalid;
    string structName;
};

struct CodegenVariable
{
    TypeDesc type{TypeDesc::Builtin(ValueType::Invalid)};
    bool isMutable = false;
    bool allocated = false;
    bool initialized = false;
    string pointer;
};

class CodeGenerator : public ASTVisitor
{
public:
    CodeGenerator(IRContext& ctx,
                  const std::unordered_map<SymbolID, VariableInfo>& symbols,
                  const std::unordered_map<string, SemanticAnalyzer::StructInfo>& structs,
                  const std::unordered_map<string, SemanticAnalyzer::FunctionInfo>& functions)
        : ctx(ctx), structs(structs), functions(functions)
    {
        for (const auto& [id, info] : symbols)
        {
            CodegenVariable var;
            var.type = info.type;
            var.isMutable = info.isMutable;
            var.pointer = "%" + info.name + "." + std::to_string(id);
            variables.emplace(id, std::move(var));
        }
    }

    void emitTopLevel(const ProgramNode& program)
    {
        emittingTopLevel = true;
        emittedStructs.clear();
        program.accept(*this);
        emittingTopLevel = false;
    }

    void generate(const ProgramNode& program)
    {
        currentBlockTerminated = false;
        program.accept(*this);
    }

    void generateFunction(const FunctionNode& func)
    {
        std::string retTy;
        if (func.returnType().kind == TypeDesc::Kind::Builtin)
            retTy = llvmType(func.returnType().builtin);
        else
            retTy = "%struct." + func.returnType().structName;

        ctx.ir << "define " << retTy << " @" << func.name() << "(";
        for (size_t i = 0; i < func.params().size(); ++i)
        {
            if (i > 0) ctx.ir << ", ";
            const auto& p = func.params()[i];
            if (p.type.kind == TypeDesc::Kind::Builtin)
                ctx.ir << llvmType(p.type.builtin) << " %" << p.name;
            else
                ctx.ir << "%struct." << p.type.structName << " %" << p.name;
        }
        ctx.ir << ") {\n";

        auto fit = functions.find(func.name());
        if (fit != functions.end())
        {
            for (const auto& p : fit->second.params)
            {
                CodegenVariable& var = getVariable(p.symbolId);
                ensureAllocated(var);
                if (p.type.kind == TypeDesc::Kind::Builtin)
                {
                    emitInstruction("store " + llvmType(p.type.builtin) + " %" + p.name + ", " + llvmType(p.type.builtin) + "* " + var.pointer);
                }
                else
                {
                    emitInstruction("store %struct." + p.type.structName + " %" + p.name + ", %struct." + p.type.structName + "* " + var.pointer);
                }
                var.initialized = true;
            }
        }

    inFunction = true;
    currentMemberMaster = func.isMember() ? func.masterStruct() : "";
    currentMemberFunctionName = func.name();
    selfSymbolId = InvalidSymbolID;
        currentBlockTerminated = false;
        functionReturnType = func.returnType();
        bool fallsThrough = true;
        if (func.body())
            fallsThrough = generateBlock(*func.body(), "");
        if (fallsThrough)
        {
            if (func.returnType().kind == TypeDesc::Kind::Builtin)
                emitInstruction(std::string("ret ") + llvmType(func.returnType().builtin) + " 0");
            else
                emitInstruction("ret %struct." + func.returnType().structName + " zeroinitializer");
        }
        inFunction = false;
        currentMemberMaster.clear();
        currentMemberFunctionName.clear();
        ctx.ir << "}\n";
    }

    void visitProgram(const ProgramNode& node) override
    {
        if (emittingTopLevel)
        {
            for (const auto& stmt : node.statements())
            {
                if (!stmt) continue;
                if (const auto* func = dynamic_cast<const FunctionNode*>(stmt.get()))
                {
                    visitFunction(*func);
                    continue;
                }
                if (const auto* sd = dynamic_cast<const StructDeclNode*>(stmt.get()))
                {
                    visitStructDecl(*sd);
                    continue;
                }
            }
            return;
        }

        for (const auto& stmt : node.statements())
        {
            if (currentBlockTerminated)
                break;
            if (!stmt)
                continue;
            if (dynamic_cast<const FunctionNode*>(stmt.get()) || dynamic_cast<const StructDeclNode*>(stmt.get()))
                continue;
            stmt->accept(*this);
        }
    }

    void visitBlock(const BlockNode& node) override
    {
        generateBlock(node, "");
    }

    void visitFunction(const FunctionNode& node) override
    {
        if (emittingTopLevel)
            generateFunction(node);
    }

    void visitDecl(const DeclNode& node) override
    {
        SymbolID symbolId = node.symbolId();
        if (symbolId == InvalidSymbolID)
            return;

        CodegenVariable& var = getVariable(symbolId);
        ensureAllocated(var);

        if (var.type.kind == TypeDesc::Kind::Builtin)
        {
            CodegenValue value{"0", false, var.type.builtin, ""};
            if (node.hasInitializer() && !node.initializers().empty())
            {
                const ExprNode* init = node.initializers()[0].get();
                init->accept(*this);
                value = popValue();
                value = ensureType(std::move(value), var.type.builtin);
            }
            storeValue(var, value);
        }
        else
        {
            if (!node.hasInitializer() || node.initializers().empty())
            {
                emitInstruction("store %struct." + var.type.structName + " zeroinitializer, %struct." + var.type.structName + "* " + var.pointer);
                var.initialized = true;
            }
            else
            {
                const auto& s = structs.at(var.type.structName);
                size_t n = std::min(node.initializers().size(), s.fields.size());
                for (size_t i = 0; i < n; ++i)
                {
                    const auto& field = s.fields[i];
                    string fieldPtr = nextTemp();
                    emitInstruction(fieldPtr + " = getelementptr %struct." + s.name + ", %struct." + s.name + "* " + var.pointer + ", i32 0, i32 " + std::to_string(i));

                    const ExprNode* expr = node.initializers()[i].get();
                    expr->accept(*this);
                    CodegenValue val = popValue();
                    if (field.type.kind == TypeDesc::Kind::Builtin)
                    {
                        val = ensureType(std::move(val), field.type.builtin);
                        emitInstruction("store " + llvmType(field.type.builtin) + " " + val.operand + ", " + llvmType(field.type.builtin) + "* " + fieldPtr);
                    }
                    else
                    {
                        if (!val.isStruct)
                        {
                            string zeroTmp = nextTemp();
                            emitInstruction(zeroTmp + " = insertvalue %struct." + field.type.structName + " undef, i32 0, 0");
                            val = {zeroTmp, true, ValueType::Invalid, field.type.structName};
                        }
                        emitInstruction("store %struct." + field.type.structName + " " + val.operand + ", %struct." + field.type.structName + "* " + fieldPtr);
                    }
                }
                var.initialized = true;
            }
        }
    }

    void visitStructDecl(const StructDeclNode& node) override
    {
        if (!emittingTopLevel)
            return;
        if (emittedStructs.find(node.name()) != emittedStructs.end())
            return;
        emittedStructs.insert(node.name());

        auto it = structs.find(node.name());
        if (it != structs.end())
        {
            ctx.ir << "%struct." << node.name() << " = type {";
            const auto& s = it->second;
            for (size_t i = 0; i < s.fields.size(); ++i)
            {
                if (i > 0) ctx.ir << ", ";
                const auto& f = s.fields[i];
                if (f.type.kind == TypeDesc::Kind::Builtin)
                    ctx.ir << llvmType(f.type.builtin);
                else
                    ctx.ir << "%struct." << f.type.structName;
            }
            ctx.ir << "}\n";

            for (const auto& mf : node.functions())
            {
                if (mf) generateFunction(*mf);
            }
            return;
        }

        ctx.ir << "%struct." << node.name() << " = type {";
        for (size_t i = 0; i < node.fields().size(); ++i)
        {
            if (i > 0) ctx.ir << ", ";
            const auto& f = node.fields()[i];
            if (f.type.kind == TypeDesc::Kind::Builtin)
                ctx.ir << llvmType(f.type.builtin);
            else
                ctx.ir << "%struct." << f.type.structName;
        }
        ctx.ir << "}\n";

        for (const auto& mf : node.functions())
        {
            if (mf) generateFunction(*mf);
        }
    }

    void visitAssign(const AssignNode& node) override
    {
        SymbolID symbolId = node.symbolId();
        if (symbolId == InvalidSymbolID)
            return;

        CodegenVariable& var = getVariable(symbolId);
        ensureAllocated(var);

        if (const ExprNode* valueExpr = node.value())
        {
            valueExpr->accept(*this);
            CodegenValue value = popValue();
            if (var.type.kind == TypeDesc::Kind::Builtin)
                value = ensureType(std::move(value), var.type.builtin);
            storeValue(var, value);
        }
    }

    void visitAssignField(const AssignFieldNode& node) override
    {
        const FieldAccessNode* fieldAccess = node.target();
        if (!fieldAccess) 
            return;

        string fieldPtr = getFieldPointer(*fieldAccess);
        if (fieldPtr.empty()) return;
        if (const ExprNode* valueExpr = node.value())
        {
            valueExpr->accept(*this);
            CodegenValue value = popValue();
            TypeDesc ftype = fieldTypeDesc(*fieldAccess);
            if (ftype.kind == TypeDesc::Kind::Builtin)
            {
                value = ensureType(std::move(value), ftype.builtin);
                emitInstruction("store " + llvmType(ftype.builtin) + " " + value.operand + ", " + llvmType(ftype.builtin) + "* " + fieldPtr);
            }
        }
    }

    void visitFieldAccess(const FieldAccessNode& node) override
    {
        string fieldPtr = getFieldPointer(node);
        if (fieldPtr.empty())
        {
            pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
            return;
        }
        TypeDesc ftype = fieldTypeDesc(node);
        if (ftype.kind == TypeDesc::Kind::Builtin)
        {
            string tmp = nextTemp();
            emitInstruction(tmp + " = load " + llvmType(ftype.builtin) + ", " + llvmType(ftype.builtin) + "* " + fieldPtr);
            pushValue({tmp, false, ftype.builtin, ""});
        }
        else
        {
            string tmp = nextTemp();
            emitInstruction(tmp + " = load %struct." + ftype.structName + ", %struct." + ftype.structName + "* " + fieldPtr);
            pushValue({tmp, true, ValueType::Invalid, ftype.structName});
        }
    }

    void visitFunctionCall(const FunctionCallNode& node) override
    {
        auto fit = functions.find(node.name());
        if (fit == functions.end())
        {
            pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
            return;
        }
        const auto& f = fit->second;

        std::vector<CodegenValue> args;
        for (size_t i = 0; i < node.args().size(); ++i)
        {
            const ExprNode* e = node.args()[i].get();
            e->accept(*this);
            CodegenValue cv = popValue();
            if (i < f.params.size() && f.params[i].type.kind == TypeDesc::Kind::Builtin)
                cv = ensureType(std::move(cv), f.params[i].type.builtin);
            args.push_back(std::move(cv));
        }
        std::string retTyIR;
        bool retIsStruct = (f.returnType.kind == TypeDesc::Kind::Struct);
        if (retIsStruct)
            retTyIR = "%struct." + f.returnType.structName;
        else
            retTyIR = llvmType(f.returnType.builtin);

    std::string tmp = nextTemp();
        std::ostringstream argss;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0) argss << ", ";
            if (f.params[i].type.kind == TypeDesc::Kind::Struct)
                argss << "%struct." << f.params[i].type.structName << " " << args[i].operand;
            else
                argss << llvmType(f.params[i].type.builtin) << " " << args[i].operand;
        }
        std::string callLine = tmp + " = call " + retTyIR + " @" + node.name() + "(" + argss.str() + ")";
        emitInstruction(callLine);

        if (retIsStruct)
            pushValue({tmp, true, ValueType::Invalid, f.returnType.structName});
        else
            pushValue({tmp, false, f.returnType.builtin, ""});
    }

    void visitMemberFunctionCall(const MemberFunctionCallNode& node) override
    {
        SymbolID baseId = node.baseSymbolId();
        if (baseId == InvalidSymbolID)
        {
            pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
            return;
        }
        CodegenVariable& baseVar = getVariable(baseId);
        ensureAllocated(baseVar);

        TypeDesc current = baseVar.type;
        string basePtr = baseVar.pointer;
        for (const std::string& funcName : node.fieldChain())
        {
            auto it = structs.find(current.structName);
            if (it == structs.end())
            {
                pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
                return;
            }
            int idx = -1; TypeDesc nextType = TypeDesc::Builtin(ValueType::Invalid);
            for (size_t i = 0; i < it->second.fields.size(); ++i)
            {
                if (it->second.fields[i].name == funcName) 
                { 
                    idx = (int)i; 
                    nextType = it->second.fields[i].type; 
                    break;
                }
            }
            if (idx < 0) { pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""}); return; }
            string gep = nextTemp();
            emitInstruction(gep + " = getelementptr %struct." + it->second.name + ", %struct." + it->second.name + "* " + basePtr + ", i32 0, i32 " + std::to_string(idx));
            basePtr = gep;
            current = nextType;
        }

        string structVal = nextTemp();
        emitInstruction(structVal + " = load %struct." + current.structName + ", %struct." + current.structName + "* " + basePtr);

        const auto* funcInfo = findMemberFunction(node.funcName(), current.structName);
        if (!funcInfo)
        {
            pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
            return;
        }
        std::vector<CodegenValue> args;

        args.push_back({structVal, true, ValueType::Invalid, current.structName});
        for (size_t i = 0; i < node.args().size(); ++i)
        {
            const ExprNode* expr = node.args()[i].get();
            expr->accept(*this);

            CodegenValue cv = popValue();
            if (i + 1 < funcInfo->params.size() && funcInfo->params[i+1].type.kind == TypeDesc::Kind::Builtin)
                cv = ensureType(std::move(cv), funcInfo->params[i+1].type.builtin);

            args.push_back(std::move(cv));
        }
        std::string retTyIR = (funcInfo->returnType.kind == TypeDesc::Kind::Struct) ? ("%struct." + funcInfo->returnType.structName) : llvmType(funcInfo->returnType.builtin);
        std::string tmp = nextTemp();
        std::ostringstream argss;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0) 
                argss << ", ";

            const auto& formal = funcInfo->params[i];
            if (formal.type.kind == TypeDesc::Kind::Struct)
                argss << "%struct." << formal.type.structName << " " << args[i].operand;
            else
                argss << llvmType(formal.type.builtin) << " " << args[i].operand;
        }
        emitInstruction(tmp + " = call " + retTyIR + " @" + node.funcName() + "(" + argss.str() + ")");
        if (funcInfo->returnType.kind == TypeDesc::Kind::Struct)
            pushValue({tmp, true, ValueType::Invalid, funcInfo->returnType.structName});
        else
            pushValue({tmp, false, funcInfo->returnType.builtin, ""});
    }

    void visitIf(const IfNode& node) override
    {
        if (!node.condition() || !node.thenBlock())
            return;

        node.condition()->accept(*this);
        CodegenValue condValue = popValue();
        condValue = ensureType(std::move(condValue), ValueType::Bool);

        string thenLabel = nextLabel("then");
        string endLabel = nextLabel("endif");
        bool hasElse = node.elseBlock() != nullptr;
        string elseLabel = hasElse ? nextLabel("else") : "";

        string falseLabel = hasElse ? elseLabel : endLabel;

        emitInstruction("br i1 " + condValue.operand + ", label %" + thenLabel + ", label %" + falseLabel);
        currentBlockTerminated = true;

        emitLabel(thenLabel);
        bool thenFallsThrough = generateBlock(*node.thenBlock(), endLabel);

        bool elseFallsThrough = false;
        if (hasElse)
        {
            emitLabel(elseLabel);
            elseFallsThrough = generateBlock(*node.elseBlock(), endLabel);
        }

        emitLabel(endLabel);

        if (hasElse && !thenFallsThrough && !elseFallsThrough)
            currentBlockTerminated = true;
        else
            currentBlockTerminated = false;
    }

    void visitReturn(const ReturnNode& node) override
    {
        if (!node.expr())
            return;

        node.expr()->accept(*this);
        CodegenValue value = popValue();
        emitReturn(std::move(value));
    }

    void visitBinaryOp(const BinaryOpNode& node) override
    {
        if (const ExprNode* left = node.left())
            left->accept(*this);
        if (const ExprNode* right = node.right())
            right->accept(*this);

        CodegenValue rightValue = popValue();
        CodegenValue leftValue = popValue();

        switch (node.op())
        {
            case BinaryOpNode::Operator::Add:
            case BinaryOpNode::Operator::Sub:
            case BinaryOpNode::Operator::Mul:
            {
                ValueType targetType = node.type();
                leftValue = ensureType(std::move(leftValue), targetType);
                rightValue = ensureType(std::move(rightValue), targetType);

                const char* opInstr = (node.op() == BinaryOpNode::Operator::Add) ? "add" :
                                      (node.op() == BinaryOpNode::Operator::Sub) ? "sub" : "mul";
                string tmp = nextTemp();
                emitInstruction(tmp + " = " + opInstr + " " + llvmType(targetType) + " " +
                                leftValue.operand + ", " + rightValue.operand);
                pushValue({tmp, false, targetType, ""});
                return;
            }
            case BinaryOpNode::Operator::Equal:
            case BinaryOpNode::Operator::NotEqual:
            {
                ValueType operandType = comparisonOperandType(leftValue.type, rightValue.type);
                leftValue = ensureType(std::move(leftValue), operandType);
                rightValue = ensureType(std::move(rightValue), operandType);

                const char* cmp = (node.op() == BinaryOpNode::Operator::Equal) ? "icmp eq" : "icmp ne";
                string tmp = nextTemp();
                emitInstruction(tmp + " = " + cmp + " " + llvmType(operandType) + " " +
                                leftValue.operand + ", " + rightValue.operand);
                pushValue({tmp, false, ValueType::Bool, ""});
                return;
            }
        }

    pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
    }

    void visitUnaryOp(const UnaryOpNode& node) override
    {
        if (const ExprNode* operand = node.operand())
            operand->accept(*this);

        CodegenValue value = popValue();
        value = ensureType(std::move(value), ValueType::Bool);
        string tmp = nextTemp();
        emitInstruction(tmp + " = xor i1 " + value.operand + ", 1");
    pushValue({tmp, false, ValueType::Bool, ""});
    }

    void visitID(const IDNode& node) override
    {
        SymbolID symbolId = node.symbolId();
        if (symbolId == InvalidSymbolID)
        {
            if (!currentMemberMaster.empty())
            {
                if (selfSymbolId == InvalidSymbolID)
                {
                    auto it = functions.find(currentMemberFunctionName);
                    if (it != functions.end())
                    {
                        for (const auto& param : it->second.params)
                        {
                            if (param.name == "_self") 
                            { 
                                selfSymbolId = param.symbolId; 
                                break; 
                            }
                        }
                    }
                }
                if (selfSymbolId != InvalidSymbolID)
                {
                    auto sit = structs.find(currentMemberMaster);
                    if (sit != structs.end())
                    {
                        int idx = -1;
                        TypeDesc ftype = TypeDesc::Builtin(ValueType::Invalid);

                        for (size_t i = 0; i < sit->second.fields.size(); ++i)
                        {
                            if (sit->second.fields[i].name == node.name()) 
                            { 
                                idx = (int)i; ftype = sit->second.fields[i].type; 
                                break; 
                            }
                        }
                        if (idx >= 0 && ftype.kind == TypeDesc::Kind::Builtin)
                        {
                            CodegenVariable& selfVar = getVariable(selfSymbolId);
                            ensureAllocated(selfVar);
                            string gep = nextTemp();
                            emitInstruction(gep + " = getelementptr %struct." + currentMemberMaster + ", %struct." + currentMemberMaster + "* " + selfVar.pointer + ", i32 0, i32 " + std::to_string(idx));
                            string tmp2 = nextTemp();
                            emitInstruction(tmp2 + " = load " + llvmType(ftype.builtin) + ", " + llvmType(ftype.builtin) + "* " + gep);
                            pushValue({tmp2, false, ftype.builtin, ""});
                            return;
                        }
                    }
                }
            }
            pushValue({"0", false, ValueType::Invalid, ""});
            return;
        }

        CodegenVariable& var = getVariable(symbolId);
        ensureAllocated(var);
        if (!var.initialized)
        {
            if (var.type.kind == TypeDesc::Kind::Builtin)
                storeValue(var, {zeroLiteral(var.type.builtin), false, var.type.builtin, ""});
            else
                emitInstruction("store %struct." + var.type.structName + " zeroinitializer, %struct." + var.type.structName + "* " + var.pointer);
            var.initialized = true;
        }

        string tmp = nextTemp();
        if (var.type.kind == TypeDesc::Kind::Builtin)
        {
            emitInstruction(tmp + " = load " + llvmType(var.type.builtin) + ", " + llvmType(var.type.builtin) + "* " + var.pointer);
            pushValue({tmp, false, var.type.builtin, ""});
        }
        else
        {
            emitInstruction(tmp + " = load %struct." + var.type.structName + ", %struct." + var.type.structName + "* " + var.pointer);
            CodegenValue cv; cv.operand = tmp; cv.isStruct = true; cv.structName = var.type.structName; cv.type = ValueType::Invalid;
            pushValue(std::move(cv));
        }
    }

    void visitNumber(const NumberNode& node) override
    {
        std::int64_t value = node.value();
        CodegenValue out;
        if (value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max())
        {
            out.type = ValueType::I32;
            out.operand = std::to_string(static_cast<std::int32_t>(value));
        }
        else
        {
            out.type = ValueType::I64;
            out.operand = std::to_string(value);
        }
        out.isStruct = false;
        out.structName.clear();
        pushValue(std::move(out));
    }

    void visitBoolLiteral(const BoolLiteralNode& node) override
    {
        pushValue({node.value() ? "1" : "0", false, ValueType::Bool, ""});
    }

private:
    const SemanticAnalyzer::FunctionInfo* findMemberFunction(const string& funcName, const string& structName) const
    {
        auto it = functions.find(funcName);
        if (it != functions.end() && it->second.isMember && it->second.masterStruct == structName)
            return &it->second;
        for (const auto& entry : functions)
        {
            if (entry.second.name == funcName && entry.second.isMember && entry.second.masterStruct == structName)
                return &entry.second;
        }
        return nullptr;
    }

    string llvmType(ValueType type) const
    {
        switch (type)
        {
            case ValueType::I32: return "i32";
            case ValueType::I64: return "i64";
            case ValueType::Bool: return "i1";
            default: return "i32";
        }
    }

    string zeroLiteral(ValueType type) const
    {
        switch (type)
        {
            case ValueType::I32:
            case ValueType::I64:
            case ValueType::Bool:
                return "0";
            default:
                return "0";
        }
    }

    string nextTemp()
    {
        return "%t" + std::to_string(ctx.tempId++);
    }

    string nextLabel(const string& base)
    {
        return base + std::to_string(labelId++);
    }

    void emitLabel(const string& label)
    {
        ctx.ir << label << ":\n";
    }

    void emitInstruction(const string& text)
    {
        ctx.ir << "  " << text << "\n";
    }

    void pushValue(CodegenValue value)
    {
        stack.push_back(std::move(value));
    }

    CodegenValue popValue()
    {
        if (stack.empty())
            return {"0", false, ValueType::Invalid, ""};
        CodegenValue value = std::move(stack.back());
        stack.pop_back();
        return value;
    }

    TypeDesc fieldTypeDesc(const FieldAccessNode& node)
    {
        SymbolID sid = node.baseSymbolId();
        if (sid == InvalidSymbolID)
            return TypeDesc::Builtin(ValueType::Invalid);

        const auto& baseVar = variables[sid];
        TypeDesc cur = baseVar.type;
        for (const std::string& fieldName : node.fieldChain())
        {
            auto it = structs.find(cur.structName);
            if (it == structs.end()) return TypeDesc::Builtin(ValueType::Invalid);
            int idx = -1; TypeDesc nextType = TypeDesc::Builtin(ValueType::Invalid);
            for (size_t i = 0; i < it->second.fields.size(); ++i)
            {
                if (it->second.fields[i].name == fieldName)
                { 
                    idx = static_cast<int>(i); 
                    nextType = it->second.fields[i].type; 
                    break; 
                }
            }
            if (idx < 0) 
                return TypeDesc::Builtin(ValueType::Invalid);
            cur = nextType;
        }
        return cur;
    }

    string getFieldPointer(const FieldAccessNode& node)
    {
        SymbolID sid = node.baseSymbolId();
        if (sid == InvalidSymbolID)
            return {};

        CodegenVariable& base = getVariable(sid);
        ensureAllocated(base);
        string ptr = base.pointer;
        TypeDesc cur = base.type;
        for (const std::string& fieldName : node.fieldChain())
        {
            auto it = structs.find(cur.structName);
            if (it == structs.end()) return {};
            int idx = -1; TypeDesc nextType = TypeDesc::Builtin(ValueType::Invalid);
            for (size_t i = 0; i < it->second.fields.size(); ++i)
            {
                if (it->second.fields[i].name == fieldName)
                { idx = static_cast<int>(i); nextType = it->second.fields[i].type; break; }
            }
            if (idx < 0) return {};
            string gep = nextTemp();
            emitInstruction(gep + " = getelementptr %struct." + it->second.name + ", %struct." + it->second.name + "* " + ptr + ", i32 0, i32 " + std::to_string(idx));
            ptr = gep;
            cur = nextType;
        }
        return ptr;
    }

    CodegenVariable& getVariable(SymbolID id)
    {
        auto it = variables.find(id);
        if (it == variables.end())
            it = variables.emplace(id, CodegenVariable{}).first;
        CodegenVariable& var = it->second;
        if (var.pointer.empty())
            var.pointer = "%tmpvar." + std::to_string(id);
        return var;
    }

    void ensureAllocated(CodegenVariable& var)
    {
        if (!var.allocated && !var.pointer.empty())
        {
            if (var.type.kind == TypeDesc::Kind::Builtin)
                emitInstruction(var.pointer + " = alloca " + llvmType(var.type.builtin));
            else
                emitInstruction(var.pointer + " = alloca %struct." + var.type.structName);
            var.allocated = true;
        }
    }

    CodegenValue ensureType(CodegenValue value, ValueType target)
    {
        if (value.isStruct)
            return {value.operand, true, ValueType::Invalid, value.structName};
        if (target == ValueType::Invalid || value.type == ValueType::Invalid)
            return {value.operand, false, ValueType::Invalid, ""};

        if (value.type == target)
            return value;

        if (target == ValueType::I64 && value.type == ValueType::I32)
        {
            string tmp = nextTemp();
            emitInstruction(tmp + " = sext i32 " + value.operand + " to i64");
            return {tmp, false, ValueType::I64, ""};
        }

        if (target == ValueType::I32 && value.type == ValueType::I64)
        {
            string tmp = nextTemp();
            emitInstruction(tmp + " = trunc i64 " + value.operand + " to i32");
            return {tmp, false, ValueType::I32, ""};
        }

        if (target == ValueType::I32 && value.type == ValueType::Bool)
        {
            string tmp = nextTemp();
            emitInstruction(tmp + " = zext i1 " + value.operand + " to i32");
            return {tmp, false, ValueType::I32, ""};
        }

        if (target == ValueType::I64 && value.type == ValueType::Bool)
        {
            CodegenValue widened = ensureType(std::move(value), ValueType::I32);
            return ensureType(std::move(widened), ValueType::I64);
        }

        if (target == ValueType::Bool && value.type != ValueType::Bool)
            return {value.operand, false, ValueType::Invalid, ""};

        return value;
    }

    void storeValue(CodegenVariable& var, const CodegenValue& value)
    {
        ensureAllocated(var);
        if (var.type.kind == TypeDesc::Kind::Builtin)
        {
            emitInstruction("store " + llvmType(var.type.builtin) + " " + value.operand + ", " + llvmType(var.type.builtin) + "* " + var.pointer);
        }
        else
        {
            emitInstruction("store %struct." + var.type.structName + " " + value.operand + ", %struct." + var.type.structName + "* " + var.pointer);
        }
        var.initialized = true;
    }

    void emitReturn(CodegenValue value)
    {
        if (inFunction)
        {
            if (functionReturnType.kind == TypeDesc::Kind::Builtin)
            {
                value = ensureType(std::move(value), functionReturnType.builtin);
                emitInstruction(std::string("ret ") + llvmType(functionReturnType.builtin) + " " + value.operand);
            }
            else
            {
                emitInstruction("ret %struct." + functionReturnType.structName + " " + value.operand);
            }
        }
        else
        {
            value = ensureType(std::move(value), ValueType::I32);
            string fmtPtr = nextTemp();
            emitInstruction(fmtPtr + " = getelementptr [29 x i8], [29 x i8]* @fmt, i32 0, i32 0");
            emitInstruction("call i32 (i8*, ...) @printf(i8* " + fmtPtr + ", i32 " + value.operand + ")");
            emitInstruction("ret i32 " + value.operand);
        }
        currentBlockTerminated = true;
    }

    bool generateBlock(const BlockNode& node, const string& exitLabel)
    {
        bool savedTerminated = currentBlockTerminated;
        currentBlockTerminated = false;

        for (const auto& stmt : node.statements())
        {
            if (currentBlockTerminated)
                break;
            if (stmt)
                stmt->accept(*this);
        }

        bool fallsThrough = !currentBlockTerminated;
        if (fallsThrough && !exitLabel.empty())
            emitInstruction("br label %" + exitLabel);

        currentBlockTerminated = savedTerminated;
        return fallsThrough;
    }

    IRContext& ctx;
    std::unordered_map<SymbolID, CodegenVariable> variables;
    std::vector<CodegenValue> stack;
    int labelId = 0;
    bool currentBlockTerminated = false;
    const std::unordered_map<string, SemanticAnalyzer::StructInfo>& structs;
    const std::unordered_map<string, SemanticAnalyzer::FunctionInfo>& functions;
    bool inFunction = false;
    TypeDesc functionReturnType{TypeDesc::Builtin(ValueType::Invalid)};
    bool emittingTopLevel = false;
    std::unordered_set<string> emittedStructs;
    std::string currentMemberMaster;
    std::string currentMemberFunctionName;
    SymbolID selfSymbolId = InvalidSymbolID;
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
        std::cerr << "Cannot open file" << std::endl;
        return 1;
    }

    std::ostringstream buffer;
    buffer << fin.rdbuf();
    string source = buffer.str();
    fin.close();

    std::vector<Token> tokens = lexSource(source);
    SyntaxParser parser(tokens);
    std::unique_ptr<ProgramNode> program = parser.parseProgram();

    if (!program || parser.hasErrors())
    {
        const std::vector<std::string>& errs = parser.errors();
        if (errs.empty())
        {
            std::cerr << "Parse error: unable to build AST" << std::endl;
        }
        else
        {
            for (const string& err : errs)
                std::cerr << "Parse error: " << err << std::endl;
        }
        return 1;
    }

    SemanticAnalyzer semantic;
    bool semanticOk = semantic.analyze(*program);

    for (const string& warning : semantic.warnings())
        std::cerr << "Warning: " << warning << std::endl;

    if (!semanticOk)
    {
        for (const string& err : semantic.errors())
            std::cerr << "Semantic error: " << err << std::endl;
        return 1;
    }

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
    fout << ctx.ir.str();
    fout.close();

    return 0;
}