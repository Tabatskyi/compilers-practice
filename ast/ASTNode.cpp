#include "ASTNode.hpp"

std::size_t ASTNode::line() const
{
    return _line;
}

void ASTNode::setLine(std::size_t line) const
{
    _line = line;
}
