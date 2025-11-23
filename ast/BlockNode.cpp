#include "BlockNode.hpp"

#include "ASTVisitor.hpp"

BlockNode::BlockNode(StmtList statements, std::size_t scopeId)
    : _statements(std::move(statements)), _scopeId(scopeId) {}

BlockNode::StmtList& BlockNode::statements()
{
    return _statements;
}

const BlockNode::StmtList& BlockNode::statements() const
{
    return _statements;
}

std::size_t BlockNode::scopeId() const
{
    return _scopeId;
}

void BlockNode::accept(ASTVisitor& visitor) const
{
    visitor.visitBlock(*this);
}