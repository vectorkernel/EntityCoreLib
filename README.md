*** This is a to draw from repository that is lagging behind and not meant to be working. One can look at the code to see where the direction for Vector Kernel is heading. Please see https://github.com/vectorkernel/Example001_OpenGLApp, which improves the code with use of polymorphism. 

https://github.com/vectorkernel/Example001_OpenGLApp

Is the first example now meant to supercede:

https://github.com/vectorkernel/VectorKernel-Starter

which acturally is a bit more advanced. All features of the Vector Kernel will now start to be enumerated by numbered example. Each example will at some point be compiled all toghether to build Vector Kernel applicatoin which will be as full featured as I can make it.


# EntityCoreLib

Minimal static library providing core data structures and spatial indexing for vector/CAD-style applications.

---

## Overview

This library contains:

- **EntityBook**
  - Central container for entities
  - Revision tracking + `Touch()` invalidation model

- **Entities**
  - `Entity`, `EntityType`
  - `LineEntity`
  - `TextEntity`
  - `SolidRectEntity`

- **Spatial Indexing**
  - `BoundingBox`
  - Boost.Geometry R-tree wrapper (`RGeometryTree`)

This is designed as a **foundation layer** for rendering engines, CAD tools, and simulation systems.

---

## Solution Layout

The Visual Studio solution is located at:

    EntityCoreLib/EntityCoreLib.slnx

Open this file in:

- Visual Studio 2022 / 2026

---

## Key Executable

The primary interactive application is:

    EntityCoreLib_OpenGLApp

This project demonstrates:

- Rendering
- Interaction
- Camera / viewport behavior
- Integration with EntityCoreLib

---

## Visual Studio Startup Configuration

### Set Startup Project (Local)

1. Open the solution
2. In **Solution Explorer**
3. Right-click:
       EntityCoreLib_OpenGLApp
4. Click:
       Set as Startup Project

---

### Shared Startup Configuration (IMPORTANT)

This repository uses a **shared launch profile**:

    EntityCoreLib/EntityCoreLib.slnLaunch

This file defines which project runs when pressing **F5**.

---

### Modify Startup Behavior

1. Right-click the **solution**
2. Select:
       Properties
3. Navigate to:
       Common Properties → Configure Startup Projects

4. Select:
       Multiple startup projects

5. Configure:

   - EntityCoreLib_OpenGLApp → Start
   - All others → None

6. Enable:
       Share Profile

7. Click Apply → OK

---

### Commit Startup Changes (REQUIRED)

If you change startup configuration, commit the `.slnLaunch` file:

    git add EntityCoreLib/EntityCoreLib.slnLaunch
    git commit -m "Update shared startup profile"
    git push

---

## Dependencies (via vcpkg)

This repo includes a `vcpkg.json` manifest.

Visual Studio can automatically restore dependencies when vcpkg integration is enabled.

### Required ports:

- glm
- boost-geometry

---

## Logging / Diagnostics

The library has **no hard dependency** on a logging system.

Instead, it exposes a host-installed log sink:

    EntityCore::SetLogSink(EntityCore::LogSink sink)

See:

    EntityCoreLog.h

If no sink is installed, logging is a no-op.

---

### Example Log Sink

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

---

## Build Notes

- Platform: Windows
- Compiler: MSVC (Visual Studio)
- Graphics: OpenGL (via OpenGL app project)

Make sure:

- vcpkg integration is enabled
- solution is opened from the correct `.slnx` file

---

## Git Notes

### Tracked Files

- `.slnx` → solution structure
- `.slnLaunch` → shared startup configuration

### Ignored Files

These should NOT be committed:

    .vs/
    *.suo
    *.user
    x64/
    Debug/
    Release/

---

## Summary

- Open: EntityCoreLib/EntityCoreLib.slnx
- Run: EntityCoreLib_OpenGLApp
- Shared startup config: .slnLaunch
- Always commit startup changes when modified

---
