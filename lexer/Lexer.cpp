#include "Lexer.hpp"

#include <cctype>
#include <unordered_map>

namespace
{
const std::unordered_map<std::string, TokenType> kKeywordMap = {
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

TokenType classify(const std::string& ident)
{
    auto it = kKeywordMap.find(ident);
    return (it != kKeywordMap.end()) ? it->second : TokenType::Identifier;
}
}

std::vector<Token> Lexer::tokenize(const std::string& source) const
{
    std::vector<Token> out;
    enum class State { Start, Identifier, Number };
    State state = State::Start;
    std::string buffer;

    for (std::size_t i = 0; i <= source.size();)
    {
        char c = (i < source.size()) ? source[i] : '\0';
        bool atEnd = (i == source.size());
        TokenType kind;

        switch (state)
        {
        case State::Start:
            if (atEnd)
            {
                ++i;
                continue;
            }
            if (c == '\n')
            {
                out.push_back(Token{"\n", TokenType::Newline});
                ++i;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < source.size() && source[i + 1] == '/')
            {
                i += 2;
                while (i < source.size() && source[i] != '\n')
                {
                    ++i;
                }
                continue;
            }
            if (c == '=' && i + 1 < source.size() && source[i + 1] == '=')
            {
                out.push_back(Token{"==", TokenType::Equals});
                i += 2;
                continue;
            }
            if (c == '!' && i + 1 < source.size() && source[i + 1] == '=')
            {
                out.push_back(Token{"!=", TokenType::NotEqual});
                i += 2;
                continue;
            }
            if (c == '!')
            {
                out.push_back(Token{"!", TokenType::Not});
                ++i;
                continue;
            }
            if (c == '-' && i + 1 < source.size() && source[i + 1] == '>')
            {
                out.push_back(Token{"->", TokenType::Arrow});
                i += 2;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)))
            {
                buffer.assign(1, c);
                state = State::Identifier;
                ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                buffer.assign(1, c);
                state = State::Number;
                ++i;
                continue;
            }

            kind = TokenType::Newline;
            switch (c)
            {
            case '{': kind = TokenType::BlockStart; break;
            case '}': kind = TokenType::BlockEnd; break;
            case '(': kind = TokenType::LParen; break;
            case ')': kind = TokenType::RParen; break;
            case '.': kind = TokenType::Dot; break;
            case ',': kind = TokenType::Comma; break;
            case '=': kind = TokenType::Assign; break;
            case '+': kind = TokenType::Add; break;
            case '-': kind = TokenType::Sub; break;
            case '*': kind = TokenType::Mul; break;
            default:
                ++i;
                continue;
            }
            out.push_back(Token{std::string(1, c), kind});
            ++i;
            continue;

        case State::Identifier:
            if (!atEnd && std::isalnum(static_cast<unsigned char>(c)))
            {
                buffer.push_back(c);
                ++i;
                continue;
            }
            out.push_back(Token{buffer, classify(buffer)});
            buffer.clear();
            state = State::Start;
            continue;

        case State::Number:
            if (!atEnd && std::isdigit(static_cast<unsigned char>(c)))
            {
                buffer.push_back(c);
                ++i;
                continue;
            }
            out.push_back(Token{buffer, TokenType::Number});
            buffer.clear();
            state = State::Start;
            continue;
        }
    }

    out.push_back(Token{"", TokenType::EndOfFile});
    return out;
}
