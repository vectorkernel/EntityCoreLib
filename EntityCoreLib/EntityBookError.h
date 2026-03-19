
#pragma once
#include <string>

enum class EntityBookErrc
{
    InvalidCapacity,
    AllocationFailed,
    InitFailed,
    OutOfMemory
};

struct EntityBookError
{
    EntityBookErrc code{};
    std::string message;   // keep it simple; can be empty if you prefer
};
