#include "pch.h"

#include "EntityCoreLog.h"
#include <atomic>
#include <sstream>

#include "Entity.h"

namespace EntityCore
{
    static std::atomic<LogSink> g_sink{ nullptr };

    void SetLogSink(LogSink sink) noexcept
    {
        g_sink.store(sink, std::memory_order_release);
    }

    LogSink GetLogSink() noexcept
    {
        return g_sink.load(std::memory_order_acquire);
    }

    static const char* ToString(EntityType t)
    {
        switch (t)
        {
        case EntityType::Line:      return "Line";
        case EntityType::Text:      return "Text";
        case EntityType::SolidRect: return "SolidRect";
        default:                    return "Unknown";
        }
    }

    static const char* ToString(EntityTag t)
    {
        switch (t)
        {
        case EntityTag::Grid:   return "Grid";
        case EntityTag::Scene:  return "Scene";
        case EntityTag::User:   return "User";
        case EntityTag::Cursor: return "Cursor";
        case EntityTag::Hud:    return "Hud";
        default:                return "Unknown";
        }
    }

    std::string DescribeEntity(const Entity& e)
    {
        std::ostringstream os;
        os << "id=" << e.ID
           << " type=" << ToString(e.type)
           << " tag=" << ToString(e.tag)
           << " drawOrder=" << e.drawOrder
           << " layerId=" << e.layerId
           << " screenSpace=" << (e.screenSpace ? "true" : "false")
           << " selected=" << (e.selected ? "true" : "false");
        return os.str();
    }
} // namespace EntityCore
