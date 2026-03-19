#include <iostream>
#include <string_view>

#include "EntityCoreLog.h"
#include "EntityBook.h"
#include "Entity.h"        // for Entity
#include <glm/glm.hpp>     // for glm::vec3

// Simple sink: prints library logs to stdout
static void TestLogSink(EntityCore::LogLevel lvl,
    const char* category,
    const char* evt,
    std::string_view msg)
{
    const char* lvlStr = "?";
    switch (lvl)
    {
    case EntityCore::LogLevel::Trace: lvlStr = "TRACE"; break;
    case EntityCore::LogLevel::Info:  lvlStr = "INFO";  break;
    case EntityCore::LogLevel::Warn:  lvlStr = "WARN";  break;
    case EntityCore::LogLevel::Error: lvlStr = "ERROR"; break;
    default: break;
    }

    std::cout << "[" << lvlStr << "] "
        << (category ? category : "") << "::" << (evt ? evt : "")
        << " - " << msg << "\n";
}

int main()
{
    EntityCore::SetLogSink(&TestLogSink);

    std::cout << "EntityCoreLib test starting...\n";

    EntityBook book;
    book.Touch("TestApp startup"); // non-structural change (revision bump + reason)

    // Create a LINE entity (EntityBook stores Entity, not LineEntity directly)
    Entity e{};
    e.ID = 1;
    e.type = EntityType::Line;
    e.tag = EntityTag::Scene;

    // LineEntity uses vec3
    e.line.p0 = glm::vec3(0.0f, 0.0f, 0.0f);
    e.line.p1 = glm::vec3(100.0f, 50.0f, 0.0f);
    e.line.width = 2.0f; // alias for thickness per your header comment

    Entity& added = book.AddEntity(e);

    // Remove it (EntityBook uses RemoveIf)
    book.RemoveIf([&](const Entity& x) { return x.ID == added.ID; });

    book.SortByDrawOrder();
    book.Clear();

    std::cout << "EntityCoreLib test completed.\n";
    return 0;
}
