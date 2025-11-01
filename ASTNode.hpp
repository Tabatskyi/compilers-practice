#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

class ProgramNode;
class BlockNode;
class StmtNode;
class ExprNode;
class FactorNode;
class ReturnNode;
class DeclNode;
class AssignNode;
class IfNode;
class IDNode;
class NumberNode;
class BoolLiteralNode;
class BinaryOpNode;
class UnaryOpNode;
class StructDeclNode;
class FunctionNode;
class FieldAccessNode;
class FunctionCallNode;

enum class ValueType
{
	Invalid,
	I32,
	I64,
	Bool
};

struct TypeDesc
{
	enum class Kind { Builtin, Struct };

	Kind kind = Kind::Builtin;
	ValueType builtin = ValueType::Invalid;
	std::string structName;

	static TypeDesc Builtin(ValueType t)
	{
		TypeDesc d;
		d.kind = Kind::Builtin;
		d.builtin = t;
		return d;
	}

	static TypeDesc Struct(std::string name)
	{
		TypeDesc d;
		d.kind = Kind::Struct;
		d.structName = std::move(name);
		return d;
	}
};

using SymbolID = size_t;
constexpr SymbolID InvalidSymbolID = static_cast<SymbolID>(-1);

class ASTVisitor
{
public:
	virtual ~ASTVisitor() = default;

	virtual void visitProgram(const ProgramNode& node) = 0;
	virtual void visitBlock(const BlockNode& node) = 0;
	virtual void visitDecl(const DeclNode& node) = 0;
	virtual void visitAssign(const AssignNode& node) = 0;
	virtual void visitIf(const IfNode& node) = 0;
	virtual void visitReturn(const ReturnNode& node) = 0;
	virtual void visitBinaryOp(const BinaryOpNode& node) = 0;
	virtual void visitUnaryOp(const UnaryOpNode& node) = 0;
	virtual void visitID(const IDNode& node) = 0;
	virtual void visitNumber(const NumberNode& node) = 0;
	virtual void visitBoolLiteral(const BoolLiteralNode& node) = 0;
	virtual void visitStructDecl(const StructDeclNode& node) = 0;
	virtual void visitFunction(const FunctionNode& node) = 0;
	virtual void visitAssignField(const class AssignFieldNode& node) = 0;
	virtual void visitFieldAccess(const FieldAccessNode& node) = 0;
	virtual void visitFunctionCall(const FunctionCallNode& node) = 0;
};

class ASTNode
{
public:
	virtual ~ASTNode() = default;
	virtual void accept(ASTVisitor& visitor) const = 0;
};

class StmtNode : public ASTNode
{
public:
	~StmtNode() override = default;
};

class ExprNode : public ASTNode
{
public:
	~ExprNode() override = default;
	ValueType type() const { return m_type; }
	void setType(ValueType type) const { m_type = type; }

private:
	mutable ValueType m_type = ValueType::Invalid;
};

class FactorNode : public ExprNode
{
public:
	~FactorNode() override = default;
};

class ProgramNode : public ASTNode
{
public:
	using StmtList = std::vector<std::unique_ptr<StmtNode>>;

	explicit ProgramNode(StmtList stmts, size_t scopeId = 0)
		: m_statements(std::move(stmts)), m_scopeId(scopeId) {}

	const StmtList& statements() const { return m_statements; }
	size_t scopeId() const { return m_scopeId; }

	void accept(ASTVisitor& visitor) const override { visitor.visitProgram(*this); }

private:
	StmtList m_statements;
	size_t m_scopeId;
};

class BlockNode : public ASTNode
{
public:
	using StmtList = std::vector<std::unique_ptr<StmtNode>>;

	BlockNode(StmtList stmts, size_t scopeId)
		: m_statements(std::move(stmts)), m_scopeId(scopeId) {}

	const StmtList& statements() const { return m_statements; }
	size_t scopeId() const { return m_scopeId; }

	void accept(ASTVisitor& visitor) const override { visitor.visitBlock(*this); }

private:
	StmtList m_statements;
	size_t m_scopeId;
};

class IDNode : public FactorNode
{
public:
	explicit IDNode(std::string name) : m_name(std::move(name)) {}

	const std::string& name() const { return m_name; }
	SymbolID symbolId() const { return m_symbolId; }
	void setSymbolId(SymbolID id) const { m_symbolId = id; }

	void accept(ASTVisitor& visitor) const override { visitor.visitID(*this); }

private:
	std::string m_name;
	mutable SymbolID m_symbolId = InvalidSymbolID;
};

class NumberNode : public FactorNode
{
public:
	explicit NumberNode(std::int64_t value) : m_value(value) {}

	std::int64_t value() const { return m_value; }

	void accept(ASTVisitor& visitor) const override { visitor.visitNumber(*this); }

private:
	std::int64_t m_value;
};

class BoolLiteralNode : public FactorNode
{
public:
	explicit BoolLiteralNode(bool value) : m_value(value) {}

	bool value() const { return m_value; }

	void accept(ASTVisitor& visitor) const override { visitor.visitBoolLiteral(*this); }

private:
	bool m_value;
};

class BinaryOpNode : public ExprNode
{
public:
	enum class Operator
	{
		Add,
		Sub,
		Mul,
		Equal,
		NotEqual
	};

	BinaryOpNode(Operator op,
				 std::unique_ptr<ExprNode> left,
				 std::unique_ptr<ExprNode> right)
		: opKind(op),
		  leftExpr(std::move(left)),
		  rightExpr(std::move(right)) {}

	Operator op() const { return opKind; }
	const ExprNode* left() const { return leftExpr.get(); }
	const ExprNode* right() const { return rightExpr.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitBinaryOp(*this); }

private:
	Operator opKind;
	std::unique_ptr<ExprNode> leftExpr;
	std::unique_ptr<ExprNode> rightExpr;
};

class UnaryOpNode : public ExprNode
{
public:
	enum class Operator
	{
		LogicalNot
	};

	UnaryOpNode(Operator op, std::unique_ptr<ExprNode> operand)
		: opKind(op), operandExpr(std::move(operand)) {}

	Operator op() const { return opKind; }
	const ExprNode* operand() const { return operandExpr.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitUnaryOp(*this); }

private:
	Operator opKind;
	std::unique_ptr<ExprNode> operandExpr;
};

class DeclNode : public StmtNode
{
public:
	DeclNode(TypeDesc type,
			std::string identifier,
			bool isMutable,
			std::vector<std::unique_ptr<ExprNode>> initializers)
		: declType(std::move(type)),
		  varName(std::move(identifier)),
		  mutableFlag(isMutable),
		  initExprs(std::move(initializers)) {}

	const TypeDesc& declaredType() const { return declType; }
	const std::string& identifier() const { return varName; }
	bool isMutable() const { return mutableFlag; }
	bool hasInitializer() const { return !initExprs.empty(); }
	const std::vector<std::unique_ptr<ExprNode>>& initializers() const { return initExprs; }
	SymbolID symbolId() const { return symId; }
	void setSymbolId(SymbolID id) const { symId = id; }

	void accept(ASTVisitor& visitor) const override { visitor.visitDecl(*this); }

private:
	TypeDesc declType;
	std::string varName;
	bool mutableFlag;
	std::vector<std::unique_ptr<ExprNode>> initExprs;
	mutable SymbolID symId = InvalidSymbolID;
};

class AssignNode : public StmtNode
{
public:
	AssignNode(std::string identifier, std::unique_ptr<ExprNode> value)
		: targetName(std::move(identifier)), assignedValue(std::move(value)) {}

	const std::string& identifier() const { return targetName; }
	const ExprNode* value() const { return assignedValue.get(); }
	SymbolID symbolId() const { return symId; }
	void setSymbolId(SymbolID id) const { symId = id; }

	void accept(ASTVisitor& visitor) const override { visitor.visitAssign(*this); }

private:
	std::string targetName;
	std::unique_ptr<ExprNode> assignedValue;
	mutable SymbolID symId = InvalidSymbolID;
};

class FieldAccessNode : public FactorNode
{
public:
	FieldAccessNode(std::string base, std::vector<std::string> chain)
		: baseName(std::move(base)), fields(std::move(chain)) {}

	const std::string& base() const { return baseName; }
	const std::vector<std::string>& fieldChain() const { return fields; }
	SymbolID baseSymbolId() const { return baseId; }
	void setBaseSymbolId(SymbolID id) const { baseId = id; }

	void accept(ASTVisitor& visitor) const override { visitor.visitFieldAccess(*this); }

private:
	std::string baseName;
	std::vector<std::string> fields;
	mutable SymbolID baseId = InvalidSymbolID;
};

class AssignFieldNode : public StmtNode
{
public:
	AssignFieldNode(std::unique_ptr<FieldAccessNode> target, std::unique_ptr<ExprNode> value)
		: lhs(std::move(target)), rhs(std::move(value)) {}

	const FieldAccessNode* target() const { return lhs.get(); }
	const ExprNode* value() const { return rhs.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitAssignField(*this); }

private:
	std::unique_ptr<FieldAccessNode> lhs;
	std::unique_ptr<ExprNode> rhs;
};

class FunctionCallNode : public FactorNode
{
public:
	FunctionCallNode(std::string name, std::vector<std::unique_ptr<ExprNode>> args)
		: funcName(std::move(name)), arguments(std::move(args)) {}

	const std::string& name() const { return funcName; }
	const std::vector<std::unique_ptr<ExprNode>>& args() const { return arguments; }

	void accept(ASTVisitor& visitor) const override { visitor.visitFunctionCall(*this); }

private:
	std::string funcName;
	std::vector<std::unique_ptr<ExprNode>> arguments;
};

class IfNode : public StmtNode
{
public:
	IfNode(std::unique_ptr<ExprNode> condition,
	      std::unique_ptr<BlockNode> thenBlock,
	      std::unique_ptr<BlockNode> elseBlock)
				: condExpr(std::move(condition)),
					thenBlk(std::move(thenBlock)),
					elseBlk(std::move(elseBlock)) {}

		const ExprNode* condition() const { return condExpr.get(); }
		const BlockNode* thenBlock() const { return thenBlk.get(); }
		const BlockNode* elseBlock() const { return elseBlk.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitIf(*this); }

private:
	std::unique_ptr<ExprNode> condExpr;
	std::unique_ptr<BlockNode> thenBlk;
	std::unique_ptr<BlockNode> elseBlk;
};

class ReturnNode : public StmtNode
{
public:
	explicit ReturnNode(std::unique_ptr<ExprNode> expr)
		: returnExpr(std::move(expr)) {}

	const ExprNode* expr() const { return returnExpr.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitReturn(*this); }

private:
	std::unique_ptr<ExprNode> returnExpr;
};

class StructDeclNode : public StmtNode
{
public:
	struct Field
	{
		TypeDesc type;
		std::string name;
		bool isMutable = false;
	};

	StructDeclNode(std::string name, std::vector<Field> fields)
		: structName(std::move(name)), structFields(std::move(fields)) {}

	const std::string& name() const { return structName; }
	const std::vector<Field>& fields() const { return structFields; }

	void accept(ASTVisitor& visitor) const override { visitor.visitStructDecl(*this); }

private:
	std::string structName;
	std::vector<Field> structFields;
};

class FunctionNode : public StmtNode
{
public:
	struct Param
	{
		TypeDesc type;
		std::string name;
		mutable SymbolID symbolId = InvalidSymbolID;
	};

	FunctionNode(std::string name,
				 std::vector<Param> params,
				 TypeDesc returnType,
				 std::unique_ptr<BlockNode> body,
				 size_t scopeId)
		: funcName(std::move(name)),
		  funcParams(std::move(params)),
		  funcReturnType(std::move(returnType)),
		  funcBody(std::move(body)),
		  funcScopeId(scopeId) {}

	const std::string& name() const { return funcName; }
	const std::vector<Param>& params() const { return funcParams; }
	const TypeDesc& returnType() const { return funcReturnType; }
	const BlockNode* body() const { return funcBody.get(); }
	size_t scopeId() const { return funcScopeId; }

	void accept(ASTVisitor& visitor) const override { visitor.visitFunction(*this); }

private:
	std::string funcName;
	std::vector<Param> funcParams;
	TypeDesc funcReturnType;
	std::unique_ptr<BlockNode> funcBody;
	size_t funcScopeId;
};