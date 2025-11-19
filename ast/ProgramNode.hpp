#pragma once

#include <memory>
#include <vector>

#include "StmtNode.hpp"

class ProgramNode : public ASTNode
{
public:
	using StmtList = std::vector<std::unique_ptr<StmtNode>>;

	explicit ProgramNode(StmtList statements, std::size_t scopeId = 0);

	const StmtList& statements() const;
	std::size_t scopeId() const;

	void accept(ASTVisitor& visitor) const override;

private:
	StmtList _statements;
	std::size_t _scopeId;
};