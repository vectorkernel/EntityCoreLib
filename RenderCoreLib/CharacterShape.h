// CharacterShape.h (Demo3) - REPLACE ENTIRE FILE
#pragma once
#include <vector>
#include <glm/glm.hpp>

#include "LineSegment.h"
#include "hersheyfont.h"

struct CharacterShape
{
    std::vector<LineSegment> segments;
    float advance = 0.0f;

    void fromHersheyGlyph(const hershey_glyph* glyph, float scale)
    {
        segments.clear();
        advance = 0.0f;

        if (!glyph) return;

        // advance width in “Hershey units”
        advance = (float)glyph->width * scale;

        // Convert each path into line segments by connecting consecutive vertices
        const hershey_path* p = glyph->paths;
        while (p)
        {
            if (p->nverts >= 2)
            {
                for (unsigned int i = 1; i < p->nverts; ++i)
                {
                    const hershey_vertex& a = p->verts[i - 1];
                    const hershey_vertex& b = p->verts[i];

                    LineSegment seg;
                    seg.start = glm::vec3((float)a.x * scale, (float)a.y * scale, 0.0f);
                    seg.end = glm::vec3((float)b.x * scale, (float)b.y * scale, 0.0f);

                    segments.push_back(seg);
                }
            }
            p = p->next;
        }
    }
};

