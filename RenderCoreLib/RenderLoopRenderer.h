#pragma once
#include "LinePass.h"
#include "RectPass.h"
#include "Entity.h"
#include "RenderContext.h"

// Render-loop style renderer (UI, overlays, debug draw).
// You feed it every frame; it clears its submissions each BeginFrame().
class RenderLoopRenderer
{
public:
    void Init();
    void BeginFrame();
    void Submit(const Entity& e);
    void Draw(const RenderContext& ctx);

private:
    RectPass rectPass;
    LinePass linePass;
};

