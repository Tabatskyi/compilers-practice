#include "IfNode.hpp"

#include <utility>

#include "ASTVisitor.hpp"

IfNode::IfNode(std::unique_ptr<ExprNode> condition, std::unique_ptr<BlockNode> thenBlock, std::unique_ptr<BlockNode> elseBlock)
    : _condition(std::move(condition)), _thenBlock(std::move(thenBlock)), _elseBlock(std::move(elseBlock)) {}

const ExprNode* IfNode::condition() const
{
    return _condition.get();
}

BlockNode* IfNode::thenBlock()
{
    return _thenBlock.get();
}


BlockNode* IfNode::elseBlock()
{
    return _elseBlock.get();
}

void IfNode::accept(ASTVisitor& visitor) const
{
    visitor.visitIf(*this);
}