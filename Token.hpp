#pragma once

#include <string>
#include <vector>

enum class TokenType
{
    EndOfFile,
    Newline,
    Identifier,
    Number,
    Fn,
    Struct,
    Var,
    Mut,
    Return,
    If,
    Else,
    I32,
    I64,
    Bool,
    True,
    False,
    BlockStart,
    BlockEnd,
    LParen,
    RParen,
    Comma,
    Assign,
    Arrow,
    Equals,
    NotEqual,
    Not,
    Add,
    Sub,
    Mul
};

struct Token
{
    std::string lexeme;
    TokenType type;
};