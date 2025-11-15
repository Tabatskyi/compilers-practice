#pragma once

#include <string>
#include <vector>

#include "../general/Token.hpp"

class Lexer
{
public:
    std::vector<Token> tokenize(const std::string& source) const;
};
