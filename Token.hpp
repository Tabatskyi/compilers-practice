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
    I64,
    Bool,
    True,
    False,
    BlockStart,
    BlockEnd,
    Assign,
    EqualEqual,
    NotEqual,
    Add,
    Sub,
    Mul
};

struct Token
{
    std::string lexeme;
    TokenType type;
};