#include "Lexer.hpp"

TokenType classify(const std::string& ident)
{
    auto it = keywordMap.find(ident);
    return (it != keywordMap.end()) ? it->second : TokenType::Identifier;
}

void emitToken(std::string lexeme, TokenType type, std::size_t tokenLine, LexResult& result) 
{
    result.tokens.push_back(Token{std::move(lexeme), type, tokenLine});
};

LexResult Lexer::tokenize(const std::string& source) const
{
    LexResult result;
    enum class State { Start, Identifier, Number };
    State state = State::Start;
    std::string buffer;
    std::size_t line = 1;
    std::size_t tokenLine = line;

    for (std::size_t i = 0; i <= source.size();)
    {
        char ch = (i < source.size()) ? source[i] : '\0';
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
            if (ch == '\r')
            {
                ++i;
                continue;
            }
            if (ch == '\n')
            {
                emitToken("\n", TokenType::Newline, line, result);
                ++i;
                ++line;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                ++i;
                continue;
            }
            if (ch == '/' && i + 1 < source.size() && source[i + 1] == '/')
            {
                i += 2;
                while (i < source.size() && source[i] != '\n')
                {
                    ++i;
                }
                continue;
            }
            if (ch == '=' && i + 1 < source.size() && source[i + 1] == '=')
            {
                emitToken("==", TokenType::Equals, line, result);
                i += 2;
                continue;
            }
            if (ch == '!' && i + 1 < source.size() && source[i + 1] == '=')
            {
                emitToken("!=", TokenType::NotEqual, line, result);
                i += 2;
                continue;
            }
            if (ch == '!')
            {
                emitToken("!", TokenType::Not, line, result);
                ++i;
                continue;
            }
            if (ch == '-' && i + 1 < source.size() && source[i + 1] == '>')
            {
                emitToken("->", TokenType::Arrow, line, result);
                i += 2;
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(ch)))
            {
                buffer.assign(1, ch);
                tokenLine = line;
                state = State::Identifier;
                ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(ch)))
            {
                buffer.assign(1, ch);
                tokenLine = line;
                state = State::Number;
                ++i;
                continue;
            }

            kind = TokenType::Newline;
            switch (ch)
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
                result.errors.push_back(Diagnostic{"unknown symbol " + std::string(1, ch), line});
                ++i;
                continue;
            }
            emitToken(std::string(1, ch), kind, line, result);
            ++i;
            continue;

        case State::Identifier:
            if (!atEnd && std::isalnum(static_cast<unsigned char>(ch)))
            {
                buffer.push_back(ch);
                ++i;
                continue;
            }
            emitToken(buffer, classify(buffer), tokenLine, result);
            buffer.clear();
            state = State::Start;
            continue;

        case State::Number:
            if (!atEnd && std::isdigit(static_cast<unsigned char>(ch)))
            {
                buffer.push_back(ch);
                ++i;
                continue;
            }
            emitToken(buffer, TokenType::Number, tokenLine, result);
            buffer.clear();
            state = State::Start;
            continue;
        }
    }

    emitToken("", TokenType::EndOfFile, line, result);
    return result;
}