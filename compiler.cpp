#include "generator/CodeGen.hpp"
#include "lexer/Lexer.hpp"
#include "parser/SyntaxParser.hpp"
#include "semantic/Semantic.hpp"

#include <limits>
#include <sstream>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using std::string;

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
    const StructTable& structs() const { return structTable; }
    const FunctionTable& functions() const { return functionTable; }

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
            info.params.push_back(FunctionParamInfo{p.type, p.name, InvalidSymbolID});
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
    StructTable structTable;
    FunctionTable functionTable;
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

    Lexer lexer;
    std::vector<Token> tokens = lexer.tokenize(source);
    SyntaxParser parser(tokens);
    std::unique_ptr<ProgramNode> program = parser.parseProgram();

    if (!program || parser.hasErrors())
    {
        const std::vector<Diagnostic>& errs = parser.errors();
        if (errs.empty())
        {
            std::cerr << "Parse error: unable to build AST" << std::endl;
        }
        else
        {
            for (const Diagnostic& err : errs)
                std::cerr << "Parse error: " << err.message << std::endl;
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