#include "CodeGen.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

using std::string;

namespace
{
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
}

CodeGenerator::CodeGenerator(IRContext& ctx,
                             const std::unordered_map<SymbolID, VariableInfo>& symbols,
                             const StructTable& structs,
                             const FunctionTable& functions)
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

void CodeGenerator::emitTopLevel(const ProgramNode& program)
{
    emittingTopLevel = true;
    emittedStructs.clear();
    program.accept(*this);
    emittingTopLevel = false;
}

void CodeGenerator::generate(const ProgramNode& program)
{
    currentBlockTerminated = false;
    program.accept(*this);
}

void CodeGenerator::generateFunction(const FunctionNode& func)
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

void CodeGenerator::visitProgram(const ProgramNode& node)
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

void CodeGenerator::visitBlock(const BlockNode& node)
{
    generateBlock(node, "");
}

void CodeGenerator::visitFunction(const FunctionNode& node)
{
    if (emittingTopLevel)
        generateFunction(node);
}

void CodeGenerator::visitDecl(const DeclNode& node)
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

void CodeGenerator::visitStructDecl(const StructDeclNode& node)
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

void CodeGenerator::visitAssign(const AssignNode& node)
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

void CodeGenerator::visitAssignField(const AssignFieldNode& node)
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

void CodeGenerator::visitFieldAccess(const FieldAccessNode& node)
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

void CodeGenerator::visitFunctionCall(const FunctionCallNode& node)
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

void CodeGenerator::visitMemberFunctionCall(const MemberFunctionCallNode& node)
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
                idx = static_cast<int>(i);
                nextType = it->second.fields[i].type;
                break;
            }
        }
        if (idx < 0)
        {
            pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
            return;
        }
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
        if (i + 1 < funcInfo->params.size() && funcInfo->params[i + 1].type.kind == TypeDesc::Kind::Builtin)
            cv = ensureType(std::move(cv), funcInfo->params[i + 1].type.builtin);

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

void CodeGenerator::visitIf(const IfNode& node)
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

void CodeGenerator::visitReturn(const ReturnNode& node)
{
    if (!node.expr())
        return;

    node.expr()->accept(*this);
    CodegenValue value = popValue();
    emitReturn(std::move(value));
}

void CodeGenerator::visitBinaryOp(const BinaryOpNode& node)
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
            emitInstruction(tmp + " = " + std::string(opInstr) + " " + llvmType(targetType) + " " +
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
            emitInstruction(tmp + " = " + std::string(cmp) + " " + llvmType(operandType) + " " +
                            leftValue.operand + ", " + rightValue.operand);
            pushValue({tmp, false, ValueType::Bool, ""});
            return;
        }
    }

    pushValue({zeroLiteral(ValueType::Invalid), false, ValueType::Invalid, ""});
}

void CodeGenerator::visitUnaryOp(const UnaryOpNode& node)
{
    if (const ExprNode* operand = node.operand())
        operand->accept(*this);

    CodegenValue value = popValue();
    value = ensureType(std::move(value), ValueType::Bool);
    string tmp = nextTemp();
    emitInstruction(tmp + " = xor i1 " + value.operand + ", 1");
    pushValue({tmp, false, ValueType::Bool, ""});
}

void CodeGenerator::visitID(const IDNode& node)
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
                            idx = static_cast<int>(i);
                            ftype = sit->second.fields[i].type;
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

void CodeGenerator::visitNumber(const NumberNode& node)
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

void CodeGenerator::visitBoolLiteral(const BoolLiteralNode& node)
{
    pushValue({node.value() ? "1" : "0", false, ValueType::Bool, ""});
}

const FunctionInfo* CodeGenerator::findMemberFunction(const std::string& funcName, const std::string& structName) const
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

std::string CodeGenerator::llvmType(ValueType type) const
{
    switch (type)
    {
        case ValueType::I32: return "i32";
        case ValueType::I64: return "i64";
        case ValueType::Bool: return "i1";
        default: return "i32";
    }
}

std::string CodeGenerator::zeroLiteral(ValueType type) const
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

std::string CodeGenerator::nextTemp()
{
    return "%t" + std::to_string(ctx.tempId++);
}

std::string CodeGenerator::nextLabel(const std::string& base)
{
    return base + std::to_string(labelId++);
}

void CodeGenerator::emitLabel(const std::string& label)
{
    ctx.ir << label << ":\n";
}

void CodeGenerator::emitInstruction(const std::string& text)
{
    ctx.ir << "  " << text << "\n";
}

void CodeGenerator::pushValue(CodegenValue value)
{
    stack.push_back(std::move(value));
}

CodegenValue CodeGenerator::popValue()
{
    if (stack.empty())
        return {"0", false, ValueType::Invalid, ""};
    CodegenValue value = std::move(stack.back());
    stack.pop_back();
    return value;
}

TypeDesc CodeGenerator::fieldTypeDesc(const FieldAccessNode& node)
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

std::string CodeGenerator::getFieldPointer(const FieldAccessNode& node)
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
            {
                idx = static_cast<int>(i);
                nextType = it->second.fields[i].type;
                break;
            }
        }
        if (idx < 0) return {};
        string gep = nextTemp();
        emitInstruction(gep + " = getelementptr %struct." + it->second.name + ", %struct." + it->second.name + "* " + ptr + ", i32 0, i32 " + std::to_string(idx));
        ptr = gep;
        cur = nextType;
    }
    return ptr;
}

CodegenVariable& CodeGenerator::getVariable(SymbolID id)
{
    auto it = variables.find(id);
    if (it == variables.end())
        it = variables.emplace(id, CodegenVariable{}).first;
    CodegenVariable& var = it->second;
    if (var.pointer.empty())
        var.pointer = "%tmpvar." + std::to_string(id);
    return var;
}

void CodeGenerator::ensureAllocated(CodegenVariable& var)
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

CodegenValue CodeGenerator::ensureType(CodegenValue value, ValueType target)
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

void CodeGenerator::storeValue(CodegenVariable& var, const CodegenValue& value)
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

void CodeGenerator::emitReturn(CodegenValue value)
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

bool CodeGenerator::generateBlock(const BlockNode& node, const std::string& exitLabel)
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
