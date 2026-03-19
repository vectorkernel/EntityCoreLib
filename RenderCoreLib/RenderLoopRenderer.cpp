#include "pch.h"
#include "RenderLoopRenderer.h"
#include "EntityType.h"
#include "HersheyTextBuilder.h"

namespace
{
    // Compute a conservative background rect for the text box.
    // If boxWidth/boxHeight are provided, we use the defined rectangle (multi-line).
    // Otherwise we measure a single-line width and use a default line height.
    static SolidRectEntity MakeTextBackgroundRect(const TextEntity& t)
    {
        SolidRectEntity r{};
        r.color = t.backgroundColor;

        float x0 = t.position.x;
        float y0 = t.position.y;
        float x1 = t.position.x;
        float y1 = t.position.y;

        if (t.boxWidth > 0.0f && t.boxHeight > 0.0f)
        {
            x1 = x0 + t.boxWidth;
            y1 = y0 + t.boxHeight;
        }
        else
        {
            const float w = HersheyTextBuilder::MeasureTextWidth(t.text, t.font, t.scale);
            const float h = 30.0f * t.scale; // matches kLineHeight in builder
            x1 = x0 + w;
            y1 = y0 + h;
        }

        const float pad = std::max(0.0f, t.backgroundPadding);
        x0 -= pad; y0 -= pad;
        x1 += pad; y1 += pad;

        r.min = glm::vec3(x0, y0, t.position.z - 0.1f);
        r.max = glm::vec3(x1, y1, t.position.z - 0.1f);
        return r;
    }


    static void MakeTextBackgroundOutlineLines(const TextEntity& t, std::vector<LineEntity>& out)
    {
        out.clear();
        float x0 = t.position.x;
        float y0 = t.position.y;
        float x1 = t.position.x;
        float y1 = t.position.y;

        if (t.boxWidth > 0.0f && t.boxHeight > 0.0f)
        {
            x1 = x0 + t.boxWidth;
            y1 = y0 + t.boxHeight;
        }
        else
        {
            const float w = HersheyTextBuilder::MeasureTextWidth(t.text, t.font, t.scale);
            const float h = 30.0f * t.scale;
            x1 = x0 + w;
            y1 = y0 + h;
        }

        const float pad = std::max(0.0f, t.backgroundPadding);
        x0 -= pad; y0 -= pad;
        x1 += pad; y1 += pad;

        const glm::vec3 a(x0, y0, t.position.z - 0.05f);
        const glm::vec3 b(x1, y0, t.position.z - 0.05f);
        const glm::vec3 c(x1, y1, t.position.z - 0.05f);
        const glm::vec3 d(x0, y1, t.position.z - 0.05f);

        auto add = [&](const glm::vec3& p0, const glm::vec3& p1)
        {
            LineEntity l{};
            l.start = p0;
            l.end = p1;
            l.color = t.backgroundOutlineColor;
            l.width = t.backgroundOutlineThickness;
            out.push_back(l);
        };

        add(a, b);
        add(b, c);
        add(c, d);
        add(d, a);
    }

}

void RenderLoopRenderer::Init()
{
    rectPass.Init();
    linePass.Init();
}

void RenderLoopRenderer::BeginFrame()
{
    rectPass.BeginFrame();
    linePass.BeginFrame();
}

void RenderLoopRenderer::Submit(const Entity& e)
{
    switch (e.type)
    {
    case EntityType::SolidRect:
        rectPass.Submit(e.solidRect);
        break;

    case EntityType::Line:
        linePass.Submit(e.line);
        break;

    case EntityType::Text:
    {
        // Optional background wipeout for both single-line and multi-line text.
        if (e.text.backgroundEnabled)
        {
            const SolidRectEntity bg = MakeTextBackgroundRect(e.text);
            rectPass.Submit(bg);

            if (e.text.backgroundOutlineEnabled)
            {
                std::vector<LineEntity> outline;
                outline.reserve(4);
                MakeTextBackgroundOutlineLines(e.text, outline);
                for (const auto& l : outline)
                    linePass.Submit(l);
            }
        }

        std::vector<LineEntity> textLines;
        textLines.reserve(e.text.text.size() * 8);
        HersheyTextBuilder::BuildLines(e.text, textLines);

        for (const auto& l : textLines)
            linePass.Submit(l);
    }
    break;

    default:
        break;
    }
}

void RenderLoopRenderer::Draw(const RenderContext& ctx)
{
    // Backgrounds first, then text/lines.
    rectPass.DrawImmediate(ctx);
    linePass.DrawImmediate(ctx);
}
