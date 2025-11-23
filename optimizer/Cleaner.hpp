#pragma once

#include "UsageAnalyzer.hpp"

class ProgramNode;

class Cleaner
{
public:
    bool removeUnusedVariables(ProgramNode& program);
};