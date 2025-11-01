#pragma once

#include <memory>
#include <string>
#include <vector>

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
		: m_operator(op),
		  m_left(std::move(left)),
		  m_right(std::move(right)) {}

	Operator op() const { return m_operator; }
	const ExprNode* left() const { return m_left.get(); }
	const ExprNode* right() const { return m_right.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitBinaryOp(*this); }

private:
	Operator m_operator;
	std::unique_ptr<ExprNode> m_left;
	std::unique_ptr<ExprNode> m_right;
};

class UnaryOpNode : public ExprNode
{
public:
	enum class Operator
	{
		LogicalNot
	};

	UnaryOpNode(Operator op, std::unique_ptr<ExprNode> operand)
		: m_operator(op), m_operand(std::move(operand)) {}

	Operator op() const { return m_operator; }
	const ExprNode* operand() const { return m_operand.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitUnaryOp(*this); }

private:
	Operator m_operator;
	std::unique_ptr<ExprNode> m_operand;
};

class DeclNode : public StmtNode
{
public:
	DeclNode(TypeDesc type,
			std::string identifier,
			bool isMutable,
			std::vector<std::unique_ptr<ExprNode>> initializers)
		: m_type(std::move(type)),
		  m_identifier(std::move(identifier)),
		  m_isMutable(isMutable),
		  m_initializers(std::move(initializers)) {}

	const TypeDesc& declaredType() const { return m_type; }
	const std::string& identifier() const { return m_identifier; }
	bool isMutable() const { return m_isMutable; }
	bool hasInitializer() const { return !m_initializers.empty(); }
	const std::vector<std::unique_ptr<ExprNode>>& initializers() const { return m_initializers; }
	SymbolID symbolId() const { return m_symbolId; }
	void setSymbolId(SymbolID id) const { m_symbolId = id; }

	void accept(ASTVisitor& visitor) const override { visitor.visitDecl(*this); }

private:
	TypeDesc m_type;
	std::string m_identifier;
	bool m_isMutable;
	std::vector<std::unique_ptr<ExprNode>> m_initializers;
	mutable SymbolID m_symbolId = InvalidSymbolID;
};

class AssignNode : public StmtNode
{
public:
	AssignNode(std::string identifier, std::unique_ptr<ExprNode> value)
		: m_identifier(std::move(identifier)), m_value(std::move(value)) {}

	const std::string& identifier() const { return m_identifier; }
	const ExprNode* value() const { return m_value.get(); }
	SymbolID symbolId() const { return m_symbolId; }
	void setSymbolId(SymbolID id) const { m_symbolId = id; }

	void accept(ASTVisitor& visitor) const override { visitor.visitAssign(*this); }

private:
	std::string m_identifier;
	std::unique_ptr<ExprNode> m_value;
	mutable SymbolID m_symbolId = InvalidSymbolID;
};

class IfNode : public StmtNode
{
public:
	IfNode(std::unique_ptr<ExprNode> condition,
	      std::unique_ptr<BlockNode> thenBlock,
	      std::unique_ptr<BlockNode> elseBlock)
		: m_condition(std::move(condition)),
		  m_then(std::move(thenBlock)),
		  m_else(std::move(elseBlock)) {}

	const ExprNode* condition() const { return m_condition.get(); }
	const BlockNode* thenBlock() const { return m_then.get(); }
	const BlockNode* elseBlock() const { return m_else.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitIf(*this); }

private:
	std::unique_ptr<ExprNode> m_condition;
	std::unique_ptr<BlockNode> m_then;
	std::unique_ptr<BlockNode> m_else;
};

class ReturnNode : public StmtNode
{
public:
	explicit ReturnNode(std::unique_ptr<ExprNode> expr)
		: m_expr(std::move(expr)) {}

	const ExprNode* expr() const { return m_expr.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitReturn(*this); }

private:
	std::unique_ptr<ExprNode> m_expr;
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
		: m_name(std::move(name)), m_fields(std::move(fields)) {}

	const std::string& name() const { return m_name; }
	const std::vector<Field>& fields() const { return m_fields; }

	void accept(ASTVisitor& visitor) const override { visitor.visitStructDecl(*this); }

private:
	std::string m_name;
	std::vector<Field> m_fields;
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
		: m_name(std::move(name)),
		  m_params(std::move(params)),
		  m_returnType(std::move(returnType)),
		  m_body(std::move(body)),
		  m_scopeId(scopeId) {}

	const std::string& name() const { return m_name; }
	const std::vector<Param>& params() const { return m_params; }
	const TypeDesc& returnType() const { return m_returnType; }
	const BlockNode* body() const { return m_body.get(); }
	size_t scopeId() const { return m_scopeId; }

	void accept(ASTVisitor& visitor) const override { visitor.visitFunction(*this); }

private:
	std::string m_name;
	std::vector<Param> m_params;
	TypeDesc m_returnType;
	std::unique_ptr<BlockNode> m_body;
	size_t m_scopeId;
};