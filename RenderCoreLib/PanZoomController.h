#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

class PanZoomController
{
public:
    // Three ortho conventions to test
    enum class OrthoMode { Center, BottomLeft, TopLeft };

public:
    // Public state (world units)
    glm::vec2 pan{ 0.0f, 0.0f };
    float zoom = 1.0f;

    float minZoom = 0.05f;
    float maxZoom = 50.0f;

    float wheelZoomFactor = 1.12f;
    float dragSpeed = 1.0f;

    OrthoMode mode = OrthoMode::Center;

    void SetViewport(int w, int h) { viewportW = w; viewportH = h; }

    void OnMouseDown(int x, int y)
    {
        dragging = true;
        // Store plane position relative to the client center so dragging does not depend on current pan.
        lastPlane = ScreenToPlane((float)x, (float)y) - CenterOffsetPlane();
    }

    void OnMouseUp() { dragging = false; }

    void OnMouseMove(int x, int y)
    {
        if (!dragging) return;

        glm::vec2 curPlane = ScreenToPlane((float)x, (float)y) - CenterOffsetPlane();
        glm::vec2 planeDelta = curPlane - lastPlane;
        lastPlane = curPlane;

        float z = std::max(zoom, 1e-6f);
        float ySign = (mode == OrthoMode::TopLeft) ? -1.0f : 1.0f;

        // Convert plane delta (projection space units) into world delta and pan oppositely.
        glm::vec2 worldDelta;
        worldDelta.x = planeDelta.x / z;
        worldDelta.y = planeDelta.y / (z * ySign);

        pan -= (worldDelta * dragSpeed);
    }

    void OnMouseWheel(int mouseX, int mouseY, int wheelDelta)
    {
        if (viewportW <= 0 || viewportH <= 0) return;

        float steps = (float)wheelDelta / 120.0f;
        if (steps == 0.0f) return;

        glm::vec2 before = ScreenToWorld(mouseX, mouseY);

        float factor = std::pow(wheelZoomFactor, steps);
        zoom = std::clamp(zoom * factor, minZoom, maxZoom);

        glm::vec2 after = ScreenToWorld(mouseX, mouseY);

        // Keep point under cursor stable in world space.
        pan += (before - after);
    }

    glm::mat4 GetViewMatrix() const
    {
        glm::mat4 v(1.0f);

        // For BottomLeft/TopLeft, keep world origin centered by adding (w/2,h/2) in plane units.
        glm::vec2 c = CenterOffsetPlane();
        v = glm::translate(v, glm::vec3(c, 0.0f));

        // If ortho is TopLeft (y-down), flip Y in the view so world Y remains "up".
        float ySign = (mode == OrthoMode::TopLeft) ? -1.0f : 1.0f;
        v = glm::scale(v, glm::vec3(zoom, zoom * ySign, 1.0f));

        // Pan in world units.
        v = glm::translate(v, glm::vec3(-pan, 0.0f));
        return v;
    }

    glm::mat4 GetOrthoProjection() const
    {
        if (viewportW <= 0 || viewportH <= 0)
            return glm::mat4(1.0f);

        float w = (float)viewportW;
        float h = (float)viewportH;

        switch (mode)
        {
        case OrthoMode::Center:
            // origin (0,0) at center, Y up
            return glm::ortho(-w * 0.5f, +w * 0.5f,
                              -h * 0.5f, +h * 0.5f,
                              -1.0f, +1.0f);

        case OrthoMode::BottomLeft:
            // origin (0,0) bottom-left, Y up
            return glm::ortho(0.0f, w, 0.0f, h, -1.0f, +1.0f);

        case OrthoMode::TopLeft:
            // origin (0,0) top-left, Y down
            return glm::ortho(0.0f, w, h, 0.0f, -1.0f, +1.0f);
        }

        return glm::mat4(1.0f);
    }

    glm::mat4 GetMVP() const
    {
        return GetOrthoProjection() * GetViewMatrix();
    }

    glm::vec2 ScreenToWorld(int x, int y) const
    {
        glm::vec2 plane = ScreenToPlane((float)x, (float)y);

        // Make world origin sit at the client center for the two corner-origin projections.
        plane -= CenterOffsetPlane();

        float z = std::max(zoom, 1e-6f);
        float ySign = (mode == OrthoMode::TopLeft) ? -1.0f : 1.0f;

        glm::vec2 world;
        world.x = pan.x + (plane.x / z);
        world.y = pan.y + (plane.y / (z * ySign));
        return world;
    }

    // Convert world coordinates to Win32 client pixel coordinates (origin top-left, Y down).
    // Useful for drawing screen-space overlays (grips, markers) that must stay a constant pixel size.
    glm::vec2 WorldToScreen(float worldX, float worldY) const
    {
        if (viewportW <= 0 || viewportH <= 0)
            return { 0.0f, 0.0f };

        float z = std::max(zoom, 1e-6f);
        float ySign = (mode == OrthoMode::TopLeft) ? -1.0f : 1.0f;

        // World -> plane (projection-space units)
        glm::vec2 plane;
        plane.x = (worldX - pan.x) * z;
        plane.y = (worldY - pan.y) * (z * ySign);

        // For corner-origin projections, shift plane so world (0,0) sits at client center when pan=0.
        plane += CenterOffsetPlane();

        // Plane -> screen (client pixels)
        switch (mode)
        {
        case OrthoMode::Center:
            // plane origin at center, y up -> client y down
            return { plane.x + viewportW * 0.5f, (viewportH * 0.5f) - plane.y };

        case OrthoMode::BottomLeft:
            // plane origin at bottom-left, y up -> client y down
            return { plane.x, (float)viewportH - plane.y };

        case OrthoMode::TopLeft:
            // plane origin at top-left, y down -> client coords match
            return { plane.x, plane.y };
        }

        return { plane.x, plane.y };
    }


    const char* GetModeName() const
    {
        switch (mode)
        {
        case OrthoMode::Center:     return "1 Center (0,0 center, Y up)";
        case OrthoMode::BottomLeft: return "2 BottomLeft (0,0 BL, Y up)";
        case OrthoMode::TopLeft:    return "3 TopLeft (0,0 TL, Y down)";
        }
        return "Unknown";
    }

private:
    glm::vec2 ScreenToPlane(float x, float y) const
    {
        // Return plane coordinates consistent with the current ortho projection:
        // - Center: plane origin at center, y up
        // - BottomLeft: plane origin at bottom-left, y up
        // - TopLeft: plane origin at top-left, y down
        switch (mode)
        {
        case OrthoMode::Center:
            return { x - viewportW * 0.5f, (viewportH * 0.5f) - y };

        case OrthoMode::BottomLeft:
            return { x, (float)viewportH - y };

        case OrthoMode::TopLeft:
            return { x, y };
        }

        return { x, y };
    }

    glm::vec2 CenterOffsetPlane() const
    {
        // For the two corner-origin projections, offset plane by half the viewport
        // so world (0,0) always sits at the client center when pan=0.
        if (mode == OrthoMode::Center)
            return { 0.0f, 0.0f };
        return { viewportW * 0.5f, viewportH * 0.5f };
    }

private:
    int viewportW = 0;
    int viewportH = 0;

    bool dragging = false;
    glm::vec2 lastPlane{ 0.0f, 0.0f };
};