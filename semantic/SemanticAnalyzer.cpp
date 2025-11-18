#include "SemanticAnalyzer.hpp"

#include <limits>

using std::string;

namespace
{
string typeToString(ValueType type)
{
    switch (type)
    {
        case ValueType::I32: return "i32";
        case ValueType::I64: return "i64";
        case ValueType::Bool: return "bool";
        default: return "<invalid>";
    }
}

bool isNumeric(ValueType type)
{
    return type == ValueType::I32 || type == ValueType::I64;
}

ValueType widerType(ValueType lhs, ValueType rhs)
{
    if (!isNumeric(lhs) || !isNumeric(rhs))
        return ValueType::Invalid;
    if (lhs == ValueType::I64 || rhs == ValueType::I64)
        return ValueType::I64;
    return ValueType::I32;
}

ValueType comparisonOperandType(ValueType lhs, ValueType rhs)
{
    if (lhs == ValueType::Bool && rhs == ValueType::Bool)
        return ValueType::Bool;
    return widerType(lhs, rhs);
}

bool isAssignable(ValueType target, ValueType source)
{
    if (target == ValueType::Invalid || source == ValueType::Invalid)
        return false;
    if (target == source)
        return true;
    if (target == ValueType::I64 && source == ValueType::I32)
        return true;
    return false;
}

bool canConvertToI32(ValueType type)
{
    return type == ValueType::I32 || type == ValueType::I64 || type == ValueType::Bool;
}
}

bool SemanticAnalyzer::analyze(const ProgramNode& program)
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
    {
        std::size_t line = 0;
        const auto& stmts = program.statements();
        if (!stmts.empty() && stmts.back())
            line = stmts.back()->line();
        addError("Program must end with a return statement", line);
    }

    return errorList.empty();
}

const std::vector<Diagnostic>& SemanticAnalyzer::errors() const
{
    return errorList;
}

const std::vector<Diagnostic>& SemanticAnalyzer::warnings() const
{
    return warningList;
}

const std::unordered_map<SymbolID, VariableInfo>& SemanticAnalyzer::symbols() const
{
    return symbolTable;
}

const StructTable& SemanticAnalyzer::structs() const
{
    return structTable;
}

const FunctionTable& SemanticAnalyzer::functions() const
{
    return functionTable;
}

void SemanticAnalyzer::visitProgram(const ProgramNode& node)
{
    enterScope(node.scopeId());
    for (const auto& stmt : node.statements())
    {
        if (stmt)
            stmt->accept(*this);
    }
    exitScope();
}

void SemanticAnalyzer::visitBlock(const BlockNode& node)
{
    enterScope(node.scopeId());
    for (const auto& stmt : node.statements())
    {
        if (stmt)
            stmt->accept(*this);
    }
    exitScope();
}

void SemanticAnalyzer::visitFunction(const FunctionNode& node)
{
    if (functionTable.count(node.name()))
    {
        addError("Function '" + node.name() + "' redeclared", node);
        return;
    }

    FunctionInfo info;
    info.name = node.name();
    info.returnType = node.returnType();
    info.scopeId = node.scopeId();
    info.isMember = node.isMember();
    info.masterStruct = node.isMember() ? node.masterStruct() : "";
    for (const auto& p : node.params())
        info.params.push_back(FunctionParamInfo{p.type, p.name, InvalidSymbolID});
    functionTable.emplace(info.name, std::move(info));

    enterScope(node.scopeId());
    for (const auto& p : node.params())
    {
        auto& scopeMap = scopeSymbols[currentScopeId()];
        if (scopeMap.count(p.name))
        {
            addError("Parameter '" + p.name + "' redeclared", node);
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

void SemanticAnalyzer::visitDecl(const DeclNode& node)
{
    const string& name = node.identifier();
    size_t scope = currentScopeId();
    auto& scopeMap = scopeSymbols[scope];
    if (scopeMap.count(name))
    {
        addError("Variable '" + name + "' redeclared", node);
        return;
    }

    if (node.declaredType().kind == TypeDesc::Kind::Builtin)
    {
        if (node.hasInitializer())
        {
            if (node.initializers().size() != 1)
            {
                addError("Builtin variable '" + name + "' must have a single initializer expression", node);
            }
            else
            {
                const ExprNode* init = node.initializers()[0].get();
                init->accept(*this);
                ValueType initType = init->type();
                if (!isAssignable(node.declaredType().builtin, initType))
                {
                    addError("Cannot initialize '" + name + "' of type " + typeToString(node.declaredType().builtin) +
                             " with value of type " + typeToString(initType), init);
                }
            }
        }
    }
    else
    {
        auto it = structTable.find(node.declaredType().structName);
        if (it == structTable.end())
        {
            addError("Unknown struct type '" + node.declaredType().structName + "'", node);
        }
        else if (node.hasInitializer())
        {
            const auto& fields = it->second.fields;
            if (node.initializers().size() != fields.size())
            {
                addError("Initializer list for struct '" + name + "' has wrong number of elements", node);
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
                        addError("Cannot initialize field '" + field.name + "' of struct '" + name + "' with incompatible type", expr);
                    }
                }
                else
                {
                    const IDNode* id = dynamic_cast<const IDNode*>(expr);
                    if (!id)
                    {
                        addError("Field '" + field.name + "' requires struct '" + field.type.structName + "' value", expr);
                    }
                    else
                    {
                        SymbolID srcId = id->symbolId();
                        if (srcId == InvalidSymbolID)
                        {
                            addError("Use of undeclared variable '" + id->name() + "' in struct initializer", id);
                        }
                        else
                        {
                            const auto& srcVar = symbolTable[srcId];
                            if (srcVar.type.kind != TypeDesc::Kind::Struct || srcVar.type.structName != field.type.structName)
                            {
                                addError("Struct field '" + field.name + "' type mismatch in initializer", expr);
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

void SemanticAnalyzer::visitAssign(const AssignNode& node)
{
    SymbolID symbolId = resolveSymbol(node.identifier());
    if (symbolId == InvalidSymbolID)
    {
        if (inFunction && !currentMemberMaster.empty())
        {
            auto it = structTable.find(currentMemberMaster);
            if (it != structTable.end())
            {
                bool foundField = false;
                TypeDesc fieldType;
                bool fieldMutable = false;
                for (const StructFieldInfo& field : it->second.fields)
                {
                    if (field.name == node.identifier())
                    {
                        foundField = true;
                        fieldType = field.type;
                        fieldMutable = field.isMutable;
                        break;
                    }
                }

                if (foundField)
                {
                    if (!fieldMutable)
                        addError("Field '" + node.identifier() + "' is immutable", node);

                    if (const ExprNode* value = node.value())
                    {
                        value->accept(*this);
                        if (!isAssignable(fieldType.builtin, value->type()))
                        {
                            addError("Cannot assign value of type " + typeToString(value->type()) + " to field '" + node.identifier() + "' of type " + typeToString(fieldType.builtin), value);
                        }
                    }
                    return;
                }
            }
        }

        addError("Assignment to undeclared variable '" + node.identifier() + "'", node);
    }
    else
    {
        const auto& info = symbolTable[symbolId];
        if (!info.isMutable)
            addError("Variable '" + node.identifier() + "' is immutable", node);
        if (info.type.kind == TypeDesc::Kind::Struct)
            addError("Assignment to struct variables is not supported", node);
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
                addError("Cannot assign value of type " + typeToString(valueType) + " to variable '" + node.identifier() + "' of type " + (info.type.kind == TypeDesc::Kind::Builtin ? typeToString(info.type.builtin) : ("struct " + info.type.structName)), value);
            }
        }
    }
}

void SemanticAnalyzer::visitIf(const IfNode& node)
{
    if (const ExprNode* cond = node.condition())
    {
        cond->accept(*this);
        if (cond->type() != ValueType::Bool)
            addError("Condition of if statement must be bool", cond);
    }

    if (const BlockNode* thenBlock = node.thenBlock())
        thenBlock->accept(*this);
    if (const BlockNode* elseBlock = node.elseBlock())
        elseBlock->accept(*this);
}

void SemanticAnalyzer::visitReturn(const ReturnNode& node)
{
    if (!node.expr())
    {
        addError("Return statement requires an expression", node);
        return;
    }
    node.expr()->accept(*this);
    if (inFunction)
    {
        if (currentFunctionReturn.kind == TypeDesc::Kind::Builtin)
        {
            ValueType t = node.expr()->type();
            if (!isAssignable(currentFunctionReturn.builtin, t))
                addError("Return type mismatch", node);
        }
        else
        {
            const IDNode* id = dynamic_cast<const IDNode*>(node.expr());
            if (!id)
                addError("Return of struct must be a variable", node);
            else
            {
                SymbolID sid = id->symbolId();
                if (sid == InvalidSymbolID)
                    addError("Return references undeclared variable", node);
                else
                {
                    const auto& v = symbolTable[sid];
                    if (v.type.kind != TypeDesc::Kind::Struct || v.type.structName != currentFunctionReturn.structName)
                        addError("Return struct type mismatch", node);
                }
            }
        }
    }
    else
    {
        returnSeen = true;
        ValueType type = node.expr()->type();
        if (!canConvertToI32(type))
            addError("Return type must be convertible to i32, got " + typeToString(type), node);
    }
}

void SemanticAnalyzer::visitBinaryOp(const BinaryOpNode& node)
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
                addError("Arithmetic operators require numeric operands", node);
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
                addError("Comparison requires compatible operand types", node);
                node.setType(ValueType::Invalid);
                return;
            }
            node.setType(ValueType::Bool);
            return;
        }
    }

    node.setType(ValueType::Invalid);
}

void SemanticAnalyzer::visitUnaryOp(const UnaryOpNode& node)
{
    if (const ExprNode* operand = node.operand())
        operand->accept(*this);

    const ExprNode* operand = node.operand();
    ValueType operandType = operand ? operand->type() : ValueType::Invalid;
    if (operandType != ValueType::Bool)
    {
        addError("Logical not operator requires bool operand", node);
        node.setType(ValueType::Invalid);
        return;
    }

    node.setType(ValueType::Bool);
}

void SemanticAnalyzer::visitID(const IDNode& node)
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
        addError("Use of undeclared variable '" + node.name() + "'", node);
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

void SemanticAnalyzer::visitNumber(const NumberNode& node)
{
    std::int64_t value = node.value();
    if (value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max())
        node.setType(ValueType::I32);
    else
        node.setType(ValueType::I64);
}

void SemanticAnalyzer::visitBoolLiteral(const BoolLiteralNode& node)
{
    node.setType(ValueType::Bool);
}

void SemanticAnalyzer::visitFieldAccess(const FieldAccessNode& node)
{
    SymbolID baseId = resolveSymbol(node.base());
    if (baseId == InvalidSymbolID)
    {
        addError("Use of undeclared variable '" + node.base() + "'", node);
        return;
    }
    node.setBaseSymbolId(baseId);
    const auto& baseVar = symbolTable[baseId];
    if (baseVar.type.kind != TypeDesc::Kind::Struct)
    {
        addError("Variable '" + node.base() + "' is not a struct", node);
        node.setType(ValueType::Invalid);
        return;
    }
    TypeDesc current = baseVar.type;
    for (size_t i = 0; i < node.fieldChain().size(); ++i)
    {
        auto it = structTable.find(current.structName);
        if (it == structTable.end())
        {
            addError("Unknown struct type '" + current.structName + "'", node);
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
            addError("Struct '" + it->second.name + "' has no field '" + fieldName + "'", node);
            node.setType(ValueType::Invalid);
            return;
        }
    }
    if (current.kind == TypeDesc::Kind::Builtin)
        node.setType(current.builtin);
    else
        node.setType(ValueType::Invalid);
}

void SemanticAnalyzer::visitAssignField(const AssignFieldNode& node)
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
        addError("Field access is immutable", node);

    if (current.kind == TypeDesc::Kind::Struct)
    {
        addError("Assignment to struct fields is not supported in this task", node);
    }

    if (const ExprNode* value = node.value())
    {
        value->accept(*this);
        if (current.kind == TypeDesc::Kind::Builtin)
        {
            if (!isAssignable(current.builtin, value->type()))
                addError("Cannot assign incompatible type to field", value);
        }
    }
}

void SemanticAnalyzer::visitMemberFunctionCall(const MemberFunctionCallNode& node)
{
    SymbolID baseId = resolveSymbol(node.base());
    if (baseId == InvalidSymbolID)
    {
        addError("Use of undeclared variable '" + node.base() + "'", node);
        return;
    }
    node.setBaseSymbolId(baseId);
    const auto& baseVar = symbolTable[baseId];

    if (baseVar.type.kind != TypeDesc::Kind::Struct)
    {
        addError("Variable '" + node.base() + "' is not a struct", node);
        return;
    }

    TypeDesc current = baseVar.type;
    for (size_t i = 0; i < node.fieldChain().size(); ++i)
    {
        auto it = structTable.find(current.structName);
        if (it == structTable.end()) { addError("Unknown struct type '" + current.structName + "'", node); return; }
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
            addError("Struct '" + it->second.name + "' has no field '" + fieldName + "'", node);
            return;
        }
    }
    if (current.kind != TypeDesc::Kind::Struct)
    {
        addError("Member function call base must resolve to a struct", node);
        return;
    }

    const FunctionInfo* funcInfo = findMemberFunction(node.funcName(), current.structName);
    if (!funcInfo)
    {
        addError("Call to undeclared member function '" + node.funcName() + "'", node);
        return;
    }

    if (funcInfo->params.empty())
    {
        addError("Corrupt member function info", node);
        return;
    }

    size_t expectedUserArgs = funcInfo->params.size() - 1;
    if (node.args().size() != expectedUserArgs)
    {
        addError("Member function '" + node.funcName() + "' called with wrong number of arguments", node);
        return;
    }

    validateCallArguments(node.args(), *funcInfo, 1, "Use of undeclared variable in member function call");

    if (funcInfo->returnType.kind == TypeDesc::Kind::Builtin)
        const_cast<MemberFunctionCallNode&>(node).setType(funcInfo->returnType.builtin);
    else
        const_cast<MemberFunctionCallNode&>(node).setType(ValueType::Invalid);
}

void SemanticAnalyzer::visitFunctionCall(const FunctionCallNode& node)
{
    auto it = functionTable.find(node.name());
    if (it == functionTable.end())
    {
        addError("Call to undeclared function '" + node.name() + "'", node);
        return;
    }
    const auto& func = it->second;
    if (node.args().size() != func.params.size())
    {
        addError("Function '" + node.name() + "' called with wrong number of arguments", node);
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

void SemanticAnalyzer::visitStructDecl(const StructDeclNode& node)
{
    if (structTable.count(node.name()))
    {
        addError("Struct '" + node.name() + "' redeclared", node);
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
                addError("Unknown struct type '" + f.type.structName + "' used in struct '" + node.name() + "'", node);
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

void SemanticAnalyzer::enterScope(size_t scopeId)
{
    scopeStack.push_back(scopeId);
    scopeSymbols.try_emplace(scopeId, std::unordered_map<string, SymbolID>{});
}

void SemanticAnalyzer::exitScope()
{
    if (!scopeStack.empty())
        scopeStack.pop_back();
}

size_t SemanticAnalyzer::currentScopeId() const
{
    return scopeStack.empty() ? 0 : scopeStack.back();
}

SymbolID SemanticAnalyzer::resolveSymbol(const string& name) const
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

void SemanticAnalyzer::addError(const string& message, std::size_t line)
{
    errorList.push_back(Diagnostic{message, line});
}

void SemanticAnalyzer::addError(const string& message, const ASTNode& node)
{
    addError(message, node.line());
}

void SemanticAnalyzer::addError(const string& message, const ASTNode* node)
{
    addError(message, node ? node->line() : 0);
}

void SemanticAnalyzer::addWarning(const string& message, std::size_t line)
{
    warningList.push_back(Diagnostic{message, line});
}

void SemanticAnalyzer::addWarning(const string& message, const ASTNode& node)
{
    addWarning(message, node.line());
}

void SemanticAnalyzer::addWarning(const string& message, const ASTNode* node)
{
    addWarning(message, node ? node->line() : 0);
}

void SemanticAnalyzer::validateCallArguments(const std::vector<std::unique_ptr<ExprNode>>& args,
                                             const FunctionInfo& funcInfo,
                                             size_t paramStartIndex,
                                             const string& undeclaredVarMessage)
{
    for (size_t i = 0; i < args.size(); ++i)
    {
        const ExprNode* arg = args[i] ? args[i].get() : nullptr;
        if (!arg)
            continue;

        arg->accept(*this);
        if (paramStartIndex + i >= funcInfo.params.size())
        {
            addError("Argument type mismatch at position " + std::to_string(i), arg);
            continue;
        }

        const auto& param = funcInfo.params[paramStartIndex + i];
        if (param.type.kind == TypeDesc::Kind::Builtin)
        {
            if (!isAssignable(param.type.builtin, arg->type()))
                addError("Argument type mismatch at position " + std::to_string(i), arg);
        }
        else
        {
            const IDNode* id = dynamic_cast<const IDNode*>(arg);
            if (!id)
            {
                addError("Struct argument must be a variable of type '" + param.type.structName + "'", arg);
            }
            else if (id->symbolId() == InvalidSymbolID)
            {
                addError(undeclaredVarMessage, id);
            }
            else
            {
                auto varIt = symbolTable.find(id->symbolId());
                if (varIt == symbolTable.end())
                {
                    addError(undeclaredVarMessage, id);
                }
                else
                {
                    const auto& var = varIt->second;
                    if (var.type.kind != TypeDesc::Kind::Struct || var.type.structName != param.type.structName)
                        addError("Struct argument type mismatch", id);
                }
            }
        }
    }
}

const FunctionInfo* SemanticAnalyzer::findMemberFunction(const string& funcName, const string& structName) const
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