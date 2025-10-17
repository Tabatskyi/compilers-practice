#include "Token.hpp"
#include "SyntaxParser.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using std::string;

struct IRContext
{
    int tempId = 0;
    std::ostringstream ir;
};

struct VariableInfo
{
    ValueType type;
    bool isMutable;
};

static const std::unordered_map<string, TokenType> keywordMap =
{
    {"var", TokenType::Var},
    {"mut", TokenType::Mut},
    {"return", TokenType::Return},
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
                    out.push_back(Token{"==", TokenType::EqualEqual});
                    i += 2;
                    continue;
                }
                if (c == '!' && i + 1 < source.size() && source[i + 1] == '=')
                {
                    out.push_back(Token{"!=", TokenType::NotEqual});
                    i += 2;
                    continue;
                }
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
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
                if (!atEnd && (std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
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

static std::string typeToString(ValueType type)
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

class SemanticAnalyzer : public ASTVisitor
{
public:
    bool analyze(const ProgramNode& program)
    {
        m_errors.clear();
        m_symbols.clear();
        m_returnSeen = false;
        program.accept(*this);
        if (!m_returnSeen)
            addError("Missing return statement");
        return m_errors.empty();
    }

    const std::vector<std::string>& errors() const { return m_errors; }
    const std::unordered_map<std::string, VariableInfo>& symbols() const { return m_symbols; }

    void visitProgram(const ProgramNode& node) override
    {
        for (const auto& stmt : node.statements())
        {
            if (stmt)
                stmt->accept(*this);
        }

        if (const auto* ret = node.returnStmt())
            ret->accept(*this);
    }

    void visitDecl(const DeclNode& node) override
    {
        const std::string& name = node.identifier();
        if (m_symbols.count(name))
        {
            addError("Variable '" + name + "' redeclared");
            return;
        }

        m_symbols.emplace(name, VariableInfo{node.declaredType(), node.isMutable()});

        if (const ExprNode* init = node.initializer())
        {
            init->accept(*this);
            ValueType initType = init->type();
            if (!isAssignable(node.declaredType(), initType))
            {
                addError("Cannot initialize '" + name + "' of type " + typeToString(node.declaredType()) +
                         " with value of type " + typeToString(initType));
            }
        }
    }

    void visitAssign(const AssignNode& node) override
    {
        auto it = m_symbols.find(node.identifier());
        if (it == m_symbols.end())
        {
            addError("Assignment to undeclared variable '" + node.identifier() + "'");
        }
        else if (!it->second.isMutable)
        {
            addError("Variable '" + node.identifier() + "' is immutable");
        }

        if (const ExprNode* value = node.value())
        {
            value->accept(*this);
            if (it != m_symbols.end())
            {
                ValueType valueType = value->type();
                if (!isAssignable(it->second.type, valueType))
                {
                    addError("Cannot assign value of type " + typeToString(valueType) +
                             " to variable '" + node.identifier() + "' of type " + typeToString(it->second.type));
                }
            }
        }
    }

    void visitReturn(const ReturnNode& node) override
    {
        m_returnSeen = true;
        if (!node.expr())
        {
            addError("Return statement requires an expression");
            return;
        }

        node.expr()->accept(*this);
        ValueType type = node.expr()->type();
        if (type != ValueType::I32)
        {
            addError("Return type must be i32, got " + typeToString(type));
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

    void visitID(const IDNode& node) override
    {
        auto it = m_symbols.find(node.name());
        if (it == m_symbols.end())
        {
            addError("Use of undeclared variable '" + node.name() + "'");
            node.setType(ValueType::Invalid);
            return;
        }
        node.setType(it->second.type);
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

private:
    void addError(const std::string& message)
    {
        m_errors.push_back(message);
    }

    std::unordered_map<std::string, VariableInfo> m_symbols;
    std::vector<std::string> m_errors;
    bool m_returnSeen = false;
};

struct CodegenValue
{
    std::string operand;
    ValueType type;
};

struct CodegenVariable
{
    ValueType type;
    bool isMutable;
    bool allocated = false;
    std::string pointer;
};

class CodeGenerator : public ASTVisitor
{
public:
    CodeGenerator(IRContext& ctx, const std::unordered_map<std::string, VariableInfo>& symbols)
        : m_ctx(ctx)
    {
        for (const auto& [name, info] : symbols)
        {
            CodegenVariable var;
            var.type = info.type;
            var.isMutable = info.isMutable;
            var.pointer = "%" + name;
            m_variables.emplace(name, var);
        }
    }

    void generate(const ProgramNode& program)
    {
        program.accept(*this);
    }

    void visitProgram(const ProgramNode& node) override
    {
        for (const auto& stmt : node.statements())
        {
            if (stmt)
                stmt->accept(*this);
        }

        if (const auto* ret = node.returnStmt())
            ret->accept(*this);
    }

    void visitDecl(const DeclNode& node) override
    {
        auto it = m_variables.find(node.identifier());
        if (it == m_variables.end())
            return;

        CodegenVariable& var = it->second;
        if (!var.allocated)
        {
            m_ctx.ir << "  " << var.pointer << " = alloca " << llvmType(var.type) << "\n";
            var.allocated = true;
        }

        CodegenValue value{zeroLiteral(var.type), var.type};
        if (const ExprNode* init = node.initializer())
        {
            init->accept(*this);
            value = popValue();
            value = ensureType(std::move(value), var.type);
        }

        storeValue(var, value);
    }

    void visitAssign(const AssignNode& node) override
    {
        auto it = m_variables.find(node.identifier());
        if (it == m_variables.end())
            return;

        CodegenVariable& var = it->second;
        if (!var.allocated)
        {
            m_ctx.ir << "  " << var.pointer << " = alloca " << llvmType(var.type) << "\n";
            var.allocated = true;
        }

        if (const ExprNode* valueExpr = node.value())
        {
            valueExpr->accept(*this);
            CodegenValue value = popValue();
            value = ensureType(std::move(value), var.type);
            storeValue(var, value);
        }
    }

    void visitReturn(const ReturnNode& node) override
    {
        if (!node.expr())
            return;

        node.expr()->accept(*this);
        CodegenValue value = popValue();
        value = ensureType(std::move(value), ValueType::I32);

        m_ctx.ir << "  %fmtptr = getelementptr [29 x i8], [29 x i8]* @fmt, i32 0, i32 0\n";
        m_ctx.ir << "  call i32 (i8*, ...) @printf(i8* %fmtptr, i32 " << value.operand << ")\n";
        m_ctx.ir << "  ret i32 " << value.operand << "\n";
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
                std::string tmp = nextTemp();
                m_ctx.ir << "  " << tmp << " = " << opInstr << " " << llvmType(targetType) << " "
                         << leftValue.operand << ", " << rightValue.operand << "\n";
                pushValue({tmp, targetType});
                return;
            }
            case BinaryOpNode::Operator::Equal:
            case BinaryOpNode::Operator::NotEqual:
            {
                ValueType operandType = comparisonOperandType(leftValue.type, rightValue.type);
                leftValue = ensureType(std::move(leftValue), operandType);
                rightValue = ensureType(std::move(rightValue), operandType);

                const char* cmp = (node.op() == BinaryOpNode::Operator::Equal) ? "icmp eq" : "icmp ne";
                std::string tmp = nextTemp();
                m_ctx.ir << "  " << tmp << " = " << cmp << " " << llvmType(operandType) << " "
                         << leftValue.operand << ", " << rightValue.operand << "\n";
                pushValue({tmp, ValueType::Bool});
                return;
            }
        }

        pushValue({zeroLiteral(ValueType::Invalid), ValueType::Invalid});
    }

    void visitID(const IDNode& node) override
    {
        auto it = m_variables.find(node.name());
        if (it == m_variables.end())
        {
            pushValue({"0", ValueType::Invalid});
            return;
        }

        CodegenVariable& var = it->second;
        if (!var.allocated)
        {
            m_ctx.ir << "  " << var.pointer << " = alloca " << llvmType(var.type) << "\n";
            var.allocated = true;
            storeValue(var, {zeroLiteral(var.type), var.type});
        }

        std::string tmp = nextTemp();
        m_ctx.ir << "  " << tmp << " = load " << llvmType(var.type) << ", "
                 << llvmType(var.type) << "* " << var.pointer << "\n";
        pushValue({tmp, var.type});
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
        pushValue(std::move(out));
    }

    void visitBoolLiteral(const BoolLiteralNode& node) override
    {
        pushValue({node.value() ? "1" : "0", ValueType::Bool});
    }

private:
    std::string llvmType(ValueType type) const
    {
        switch (type)
        {
            case ValueType::I32: return "i32";
            case ValueType::I64: return "i64";
            case ValueType::Bool: return "i1";
            default: return "i32";
        }
    }

    std::string zeroLiteral(ValueType type) const
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

    std::string nextTemp()
    {
        return "%t" + std::to_string(m_ctx.tempId++);
    }

    void pushValue(CodegenValue value)
    {
        m_stack.push_back(std::move(value));
    }

    CodegenValue popValue()
    {
        if (m_stack.empty())
            return {"0", ValueType::Invalid};
        CodegenValue value = std::move(m_stack.back());
        m_stack.pop_back();
        return value;
    }

    CodegenValue ensureType(CodegenValue value, ValueType target)
    {
        if (target == ValueType::Invalid || value.type == ValueType::Invalid)
            return {value.operand, ValueType::Invalid};

        if (value.type == target)
            return value;

        if (target == ValueType::I64 && value.type == ValueType::I32)
        {
            std::string tmp = nextTemp();
            m_ctx.ir << "  " << tmp << " = sext i32 " << value.operand << " to i64\n";
            return {tmp, ValueType::I64};
        }

        if (target == ValueType::I32 && value.type == ValueType::Bool)
        {
            std::string tmp = nextTemp();
            m_ctx.ir << "  " << tmp << " = zext i1 " << value.operand << " to i32\n";
            return {tmp, ValueType::I32};
        }

        if (target == ValueType::I64 && value.type == ValueType::Bool)
        {
            CodegenValue widened = ensureType(std::move(value), ValueType::I32);
            return ensureType(std::move(widened), ValueType::I64);
        }

        return value;
    }

    void storeValue(const CodegenVariable& var, const CodegenValue& value)
    {
        m_ctx.ir << "  store " << llvmType(var.type) << " " << value.operand
                 << ", " << llvmType(var.type) << "* " << var.pointer << "\n";
    }

    IRContext& m_ctx;
    std::unordered_map<std::string, CodegenVariable> m_variables;
    std::vector<CodegenValue> m_stack;
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

    auto tokens = lexSource(source);
    SyntaxParser parser(tokens);
    auto program = parser.parseProgram();

    if (!program || parser.hasErrors())
    {
        const auto& errs = parser.errors();
        if (errs.empty())
        {
            std::cerr << "Parse error: unable to build AST" << std::endl;
        }
        else
        {
            for (const auto& err : errs)
                std::cerr << "Parse error: " << err << std::endl;
        }
        return 1;
    }

    SemanticAnalyzer semantic;
    if (!semantic.analyze(*program))
    {
        for (const auto& err : semantic.errors())
            std::cerr << "Semantic error: " << err << std::endl;
        return 1;
    }

    IRContext ctx;
    ctx.ir << "declare i32 @printf(i8*, ...)\n\n";
    ctx.ir << "@fmt = private constant [29 x i8] c\"Program exit with result %d\\0A\\00\"\n\n";
    ctx.ir << "define i32 @main() {\n";

    CodeGenerator generator(ctx, semantic.symbols());
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