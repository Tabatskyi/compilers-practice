#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

/// Base class for all nodes in the abstract syntax tree.
class ASTNode
{
public:
	virtual ~ASTNode() = default;

	/// Perform semantic validation for the node. For now, this is a no-op stub.
	virtual void visit() const {}
};

/// Forward declarations for node types used in composite structures.
class StmtNode;
class ExprNode;
class FactorNode;
class ReturnNode;

using StmtList = std::vector<std::unique_ptr<StmtNode>>;

/// Root of the AST representing an entire program.
class ProgramNode : public ASTNode
{
public:
	ProgramNode(StmtList stmts, std::unique_ptr<ReturnNode> ret)
		: m_statements(std::move(stmts)), m_return(std::move(ret)) {}

	const StmtList& statements() const { return m_statements; }
	const ReturnNode* returnStmt() const { return m_return.get(); }

	void visit() const override {}

private:
	StmtList m_statements;
	std::unique_ptr<ReturnNode> m_return;
};

/// Base class for all statement nodes.
class StmtNode : public ASTNode
{
public:
	~StmtNode() override = default;
};

/// Base class for all expression nodes.
class ExprNode : public ASTNode
{
public:
	~ExprNode() override = default;
};

/// Base class for terminal factors in an expression.
class FactorNode : public ExprNode
{
public:
	~FactorNode() override = default;
};

/// Represents an identifier usage.
class IDNode : public FactorNode
{
public:
	explicit IDNode(std::string name) : m_name(std::move(name)) {}

	const std::string& name() const { return m_name; }

	void visit() const override {}

private:
	std::string m_name;
};

/// Represents an integer literal.
class NumberNode : public FactorNode
{
public:
	explicit NumberNode(int value) : m_value(value) {}

	int value() const { return m_value; }

	void visit() const override {}

private:
	int m_value;
};

/// Represents a binary operation between two expressions.
class BinaryOpNode : public ExprNode
{
public:
	enum class Operator
	{
		Add,
		Sub,
		Mul
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

	void visit() const override {}

private:
	Operator m_operator;
	std::unique_ptr<ExprNode> m_left;
	std::unique_ptr<ExprNode> m_right;
};

/// Statement node representing a variable declaration.
class DeclNode : public StmtNode
{
public:
	DeclNode(std::string identifier, bool isMutable, std::unique_ptr<ExprNode> initializer)
		    : m_identifier(std::move(identifier)), m_isMutable(isMutable), m_initializer(std::move(initializer)) {}

	const std::string& identifier() const { return m_identifier; }
	bool isMutable() const { return m_isMutable; }
	bool hasInitializer() const { return static_cast<bool>(m_initializer); }
	const ExprNode* initializer() const { return m_initializer.get(); }

	void visit() const override {}

private:
	std::string m_identifier;
	bool m_isMutable;
	std::unique_ptr<ExprNode> m_initializer;
};

/// Statement node representing an assignment to an existing variable.
class AssignNode : public StmtNode
{
public:
	AssignNode(std::string identifier, std::unique_ptr<ExprNode> value)
		: m_identifier(std::move(identifier)), m_value(std::move(value)) {}

	const std::string& identifier() const { return m_identifier; }
	const ExprNode* value() const { return m_value.get(); }

	void visit() const override {}

private:
	std::string m_identifier;
	std::unique_ptr<ExprNode> m_value;
};

/// Return statement node.
class ReturnNode : public ASTNode
{
public:
	explicit ReturnNode(std::unique_ptr<ExprNode> expr)
		: m_expr(std::move(expr)) {}

	const ExprNode* expr() const { return m_expr.get(); }

	void visit() const override {}

private:
	std::unique_ptr<ExprNode> m_expr;
};

