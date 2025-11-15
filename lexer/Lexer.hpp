#pragma once

#include <string>
#include <vector>

#include "../general/Diagnostics.hpp"
#include "../general/Token.hpp"

struct LexResult
{
    std::vector<Token> tokens;
    std::vector<Diagnostic> errors;
};

class Lexer
{
public:
    LexResult tokenize(const std::string& source) const;
};