#pragma once
#include <string>

#include "TextEntity.h" // defines TextEntity and forward-declares hershey_font

namespace RenderCore
{
    enum class TextInitPolicy
    {
        DebugAssertOnly,
        AssertAndMessageBox,
        Throw
    };

    void InitTextEntityOrDie(
        TextEntity& t,
        const std::string& text,
        hershey_font* font,
        TextInitPolicy policy = TextInitPolicy::AssertAndMessageBox,
        const char* context = nullptr
    );
}