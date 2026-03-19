#pragma once

#include <string>

class Application;
class CmdWindow;

// Optional embedded Python support.
//
// Enable by defining VK_ENABLE_PYTHON in your project preprocessor definitions
// and ensuring Python headers/libs are available.
//
// When disabled, methods will report a helpful message and return false.
class PythonScriptRunner
{
public:
    PythonScriptRunner() = default;

    // Initialize the embedded interpreter and bind the scripting API.
    // Safe to call multiple times.
    bool EnsureInitialized(Application* app, CmdWindow* cmdWnd);

    // Run a script file (.py). EnsureInitialized() is called automatically.
    bool RunFile(Application* app, const std::string& scriptPath, CmdWindow* cmdWnd);

    // Optional explicit shutdown.
    void Shutdown();

private:
    bool initialized = false;
};
