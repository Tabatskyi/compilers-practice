#pragma once

class ProgramNode;

class UnusedSymbolCleaner
{
public:
    bool removeUnusedVariables(ProgramNode& program);
};
