# EntityCoreLib

Minimal static library containing:
- EntityBook (entity container + revision + Touch() invalidation)
- Entity/EntityType + LineEntity/TextEntity/SolidRectEntity payloads
- BoundingBox + Boost.Geometry R-tree wrapper (RGeometryTree)

## Dependencies (via vcpkg)
This repo includes a `vcpkg.json` manifest. Visual Studio (2022/2026) can restore these automatically when vcpkg integration is enabled.

Ports:
- glm
- boost-geometry

## Logging / diagnostics
The library has **no hard dependency** on your app logger.
Instead, it exposes a host-installed log sink:

- `EntityCore::SetLogSink(EntityCore::LogSink sink)`

See `EntityCoreLog.h`.

If you do not set a sink, logging is a no-op.

Example sink (OutputDebugString):
```cpp
#include <windows.h>
#include <string>
#include "EntityCoreLog.h"

static void MySink(EntityCore::LogLevel lvl, const char* category, const char* evt, std::string_view msg)
{
    std::string line = std::string(category) + "::" + evt + " " + std::string(msg) + "\n";
    OutputDebugStringA(line.c_str());
}

void Init()
{
    EntityCore::SetLogSink(&MySink);
}
```
