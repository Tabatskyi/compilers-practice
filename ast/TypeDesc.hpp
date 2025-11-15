#pragma once

#include <cstddef>
#include <string>
#include <utility>

enum class ValueType
{
    Invalid,
    I32,
    I64,
    Bool
};

struct TypeDesc
{
    enum class Kind { Builtin, Struct };

    Kind kind = Kind::Builtin;
    ValueType builtin = ValueType::Invalid;
    std::string structName;

    static TypeDesc Builtin(ValueType t)
    {
        TypeDesc d;
        d.kind = Kind::Builtin;
        d.builtin = t;
        return d;
    }

    static TypeDesc Struct(std::string name)
    {
        TypeDesc d;
        d.kind = Kind::Struct;
        d.structName = std::move(name);
        return d;
    }
};

using SymbolID = std::size_t;
constexpr SymbolID InvalidSymbolID = static_cast<SymbolID>(-1);
