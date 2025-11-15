#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ast/TypeDesc.hpp"

struct VariableInfo
{
    TypeDesc type{TypeDesc::Builtin(ValueType::Invalid)};
    bool isMutable = false;
    std::string name;
    std::size_t scopeId = 0;
};

struct StructFieldInfo
{
    TypeDesc type{TypeDesc::Builtin(ValueType::Invalid)};
    bool isMutable = false;
    std::string name;
};

struct StructInfo
{
    std::string name;
    std::vector<StructFieldInfo> fields;
};

struct FunctionParamInfo
{
    TypeDesc type{TypeDesc::Builtin(ValueType::Invalid)};
    std::string name;
    SymbolID symbolId = InvalidSymbolID;
};

struct FunctionInfo
{
    std::string name;
    TypeDesc returnType{TypeDesc::Builtin(ValueType::Invalid)};
    std::vector<FunctionParamInfo> params;
    std::size_t scopeId = 0;
    std::string masterStruct;
    bool isMember = false;
};

using StructTable = std::unordered_map<std::string, StructInfo>;
using FunctionTable = std::unordered_map<std::string, FunctionInfo>;
