#pragma once
#include <string>

class Application;
class CmdWindow;

// Legacy stub: this sample uses CommandManager + CmdWindow directly.
// This file remains only so existing solutions that still compile CommandUi.cpp don't break.
class CommandUi
{
public:
    explicit CommandUi(Application* /*app*/, CmdWindow* /*wnd*/) {}

    void Initialize() {}
    void OnExecuteLine(const std::string& /*lineUtf8*/) {}
    void OnCancel() {}

private:
    static std::string ToUpperTrim(const std::string& s) { return s; }
};
