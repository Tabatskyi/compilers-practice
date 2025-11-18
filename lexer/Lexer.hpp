#pragma once

#include <string>
#include <vector>
#include <unordered_map>

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

static std::unordered_map<std::string, TokenType> keywordMap = 
{
    {"var", TokenType::Var},
    {"struct", TokenType::Struct},
    {"fn", TokenType::Fn},
    {"mut", TokenType::Mut},
    {"return", TokenType::Return},
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"i32", TokenType::I32},
    {"i64", TokenType::I64},
    {"bool", TokenType::Bool},
    {"true", TokenType::True},
    {"false", TokenType::False},
};