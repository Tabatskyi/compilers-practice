#pragma once

#include <memory>
#include <vector>

#include "StmtNode.hpp"

class BlockNode : public ASTNode
{
public:
	using StmtList = std::vector<std::unique_ptr<StmtNode>>;

	BlockNode(StmtList statements, std::size_t scopeId);

	StmtList& statements();
	const StmtList& statements() const;
	std::size_t scopeId() const;

	void accept(ASTVisitor& visitor) const override;

private:
	StmtList _statements;
	std::size_t _scopeId;
};