#pragma once

#include <cstddef>
#include <string>

struct Diagnostic
{
    std::string message;
    std::size_t line = 0;
};