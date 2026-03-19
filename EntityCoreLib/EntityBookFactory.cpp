#include "pch.h"
#include "EntityBookFactory.h"
#include "EntityBook.h"
#include "EntityBookError.h"
#include "EntityCoreLog.h"
#include <cstdio>
#include <new> // std::nothrow

#define LOG_INFO(...)  do { \
    char _buf[256]; \
    std::snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
    EntityCore::EmitLog(EntityCore::LogLevel::Info,  "EntityBookFactory", "CreateEntityBook", _buf); \
} while(0)

#define LOG_ERROR(...) do { \
    char _buf[256]; \
    std::snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
    EntityCore::EmitLog(EntityCore::LogLevel::Error, "EntityBookFactory", "CreateEntityBook", _buf); \
} while(0)

std::expected<std::unique_ptr<EntityBook>, EntityBookError>
CreateEntityBook(std::size_t capacity)
{
    LOG_INFO("Creating EntityBook... capacity=%zu", capacity);

    if (capacity == 0)
    {
        EntityBookError err{ EntityBookErrc::InvalidCapacity, "capacity must be > 0" };
        LOG_ERROR("EntityBook create FAILED: %s", err.message.c_str());
        return std::unexpected(err);
    }

    auto ptr = std::unique_ptr<EntityBook>(new (std::nothrow) EntityBook(capacity));
    if (!ptr)
    {
        EntityBookError err{ EntityBookErrc::OutOfMemory, "allocation failed" };
        LOG_ERROR("EntityBook create FAILED: %s", err.message.c_str());
        return std::unexpected(err);
    }

    if (!ptr->Initialize())
    {
        EntityBookError err{ EntityBookErrc::InitFailed, "Initialize() failed" };
        LOG_ERROR("EntityBook create FAILED: %s", err.message.c_str());
        return std::unexpected(err);
    }

    LOG_INFO("EntityBook created. capacity=%zu", ptr->Capacity());
    return ptr;
}