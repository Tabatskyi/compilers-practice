#include "Cleaner.hpp"

bool Cleaner::removeUnusedVariables(ProgramNode& program)
{
    bool changed = false;
    while (true)
    {
        UsageAnalyzer analyzer;
        analyzer.analyze(program);

        std::unordered_set<SymbolID> unusedIds;
        for (SymbolID id : analyzer.declarations())
        {
            if (!analyzer.usages().count(id))
                unusedIds.insert(id);
        }

        if (unusedIds.empty())
            break;

        UsageAnalyzer::pruneProgram(program, unusedIds);
        changed = true;
    }
    return changed;
}