#pragma once

#include <cstddef>

class ASTVisitor;

class ASTNode
{
public:
	virtual ~ASTNode() = default;

	std::size_t line() const;
	void setLine(std::size_t line) const;

	virtual void accept(ASTVisitor& visitor) const = 0;

protected:
	ASTNode() = default;
	ASTNode(const ASTNode&) = default;
	ASTNode& operator=(const ASTNode&) = default;

private:
	mutable std::size_t _line = 0;
};