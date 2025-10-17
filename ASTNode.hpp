#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

class ProgramNode;
class StmtNode;
class ExprNode;
class FactorNode;
class ReturnNode;
class DeclNode;
class AssignNode;
class IDNode;
class NumberNode;
class BoolLiteralNode;
class BinaryOpNode;

enum class ValueType
{
	Invalid,
	I32,
	I64,
	Bool
};

class ASTVisitor
{
public:
	virtual ~ASTVisitor() = default;

	virtual void visitProgram(const ProgramNode& node) = 0;
	virtual void visitDecl(const DeclNode& node) = 0;
	virtual void visitAssign(const AssignNode& node) = 0;
	virtual void visitReturn(const ReturnNode& node) = 0;
	virtual void visitBinaryOp(const BinaryOpNode& node) = 0;
	virtual void visitID(const IDNode& node) = 0;
	virtual void visitNumber(const NumberNode& node) = 0;
	virtual void visitBoolLiteral(const BoolLiteralNode& node) = 0;
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

	ProgramNode(StmtList stmts, std::unique_ptr<ReturnNode> ret)
		: m_statements(std::move(stmts)), m_return(std::move(ret)) {}

	const StmtList& statements() const { return m_statements; }
	const ReturnNode* returnStmt() const { return m_return.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitProgram(*this); }

private:
	StmtList m_statements;
	std::unique_ptr<ReturnNode> m_return;
};

class IDNode : public FactorNode
{
public:
	explicit IDNode(std::string name) : m_name(std::move(name)) {}

	const std::string& name() const { return m_name; }

	void accept(ASTVisitor& visitor) const override { visitor.visitID(*this); }

private:
	std::string m_name;
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

class DeclNode : public StmtNode
{
public:
	DeclNode(ValueType type,
			std::string identifier,
			bool isMutable,
			std::unique_ptr<ExprNode> initializer)
	    : m_type(type),
	      m_identifier(std::move(identifier)),
	      m_isMutable(isMutable),
	      m_initializer(std::move(initializer)) {}

	ValueType declaredType() const { return m_type; }
	const std::string& identifier() const { return m_identifier; }
	bool isMutable() const { return m_isMutable; }
	bool hasInitializer() const { return static_cast<bool>(m_initializer); }
	const ExprNode* initializer() const { return m_initializer.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitDecl(*this); }

private:
	ValueType m_type;
	std::string m_identifier;
	bool m_isMutable;
	std::unique_ptr<ExprNode> m_initializer;
};

class AssignNode : public StmtNode
{
public:
	AssignNode(std::string identifier, std::unique_ptr<ExprNode> value)
		: m_identifier(std::move(identifier)), m_value(std::move(value)) {}

	const std::string& identifier() const { return m_identifier; }
	const ExprNode* value() const { return m_value.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitAssign(*this); }

private:
	std::string m_identifier;
	std::unique_ptr<ExprNode> m_value;
};

class ReturnNode : public ASTNode
{
public:
	explicit ReturnNode(std::unique_ptr<ExprNode> expr)
		: m_expr(std::move(expr)) {}

	const ExprNode* expr() const { return m_expr.get(); }

	void accept(ASTVisitor& visitor) const override { visitor.visitReturn(*this); }

private:
	std::unique_ptr<ExprNode> m_expr;
};