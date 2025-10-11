#pragma once

#include <string>
#include <vector>

enum class TokenType
{
    EndOfFile,
    Newline,
    Identifier,
    Number,
    Var,
    Mut,
    Return,
    I32,
    BlockStart,
    BlockEnd,
    Assign,
    Add,
    Sub,
    Mul
};

struct Token
{
    std::string lexeme;
    TokenType type;
};

using TokenStream = std::vector<Token>;
