#include "PythonScriptRunner.h"
#include "Application.h"
#include "CmdWindow.h"

#include <cstdio>
#include <cstdlib>

#ifdef VK_ENABLE_PYTHON
    #include <Python.h>

namespace
{
    // Single-runner bridge for the C callbacks.
    static Application* s_app = nullptr;
    static CmdWindow*   s_cmd = nullptr;

    static void PyLog(const std::string& msg)
    {
        if (s_cmd) s_cmd->AppendTextUtf8(msg);
    }

    static PyObject* VkWrite(PyObject*, PyObject* args)
    {
        const char* s = nullptr;
        if (!PyArg_ParseTuple(args, "s", &s))
            return nullptr;
        if (s_cmd) s_cmd->AppendTextUtf8(std::string(s));
        Py_RETURN_NONE;
    }

    static PyObject* VkFlush(PyObject*, PyObject*)
    {
        Py_RETURN_NONE;
    }

    static PyObject* VkAddLine(PyObject*, PyObject* args)
    {
        float x0, y0, z0, x1, y1, z1;
        float r, g, b, a;
        float thickness = 1.0f;
        int drawOrder = 100;

        if (!PyArg_ParseTuple(args, "fffffffffff|fi",
            &x0, &y0, &z0,
            &x1, &y1, &z1,
            &r, &g, &b, &a,
            &thickness,
            &drawOrder))
            return nullptr;

        if (s_app)
            s_app->ScriptAddLine(x0, y0, z0, x1, y1, z1, r, g, b, a, thickness, drawOrder);
        Py_RETURN_NONE;
    }

    static PyObject* VkAddText(PyObject*, PyObject* args)
    {
        const char* txt = nullptr;
        float x, y, z;
        float scale = 1.0f;
        float r = 1, g = 1, b = 1, a = 1;
        int drawOrder = 200;

        if (!PyArg_ParseTuple(args, "sfff|fffffi",
            &txt, &x, &y, &z,
            &scale,
            &r, &g, &b, &a,
            &drawOrder))
            return nullptr;

        if (s_app)
            s_app->ScriptAddText(txt, x, y, z, scale, r, g, b, a, drawOrder);
        Py_RETURN_NONE;
    }

    static PyMethodDef VkMethods[] =
    {
        {"write",    VkWrite,   METH_VARARGS, "Write text to the application's command window."},
        {"flush",    VkFlush,   METH_NOARGS,  "No-op flush (for sys.stdout compatibility)."},
        {"add_line", VkAddLine, METH_VARARGS, "Add a persistent line entity."},
        {"add_text", VkAddText, METH_VARARGS, "Add a persistent text entity."},
        {nullptr, nullptr, 0, nullptr}
    };

    static PyModuleDef VkModule =
    {
        PyModuleDef_HEAD_INIT,
        "vk",
        "VectorKernel scripting API",
        -1,
        VkMethods
    };

    static PyObject* PyInit_vk()
    {
        return PyModule_Create(&VkModule);
    }
}
#endif // VK_ENABLE_PYTHON


bool PythonScriptRunner::EnsureInitialized(Application* app, CmdWindow* cmdWnd)
{
#ifndef VK_ENABLE_PYTHON
    (void)app;
    if (cmdWnd)
        cmdWnd->AppendTextUtf8(
            "[Python] Embedded Python is disabled.\n"
            "Define VK_ENABLE_PYTHON and link against Python to enable Alt+P scripting.\n");
    return false;
#else
    s_app = app;
    s_cmd = cmdWnd;

    if (initialized)
        return true;

    // Register our built-in module before Py_Initialize.
    PyImport_AppendInittab("vk", &PyInit_vk);

    Py_Initialize();
    PyEval_InitThreads();

    // Redirect stdout/stderr to our command window via the vk module.
    PyObject* sys = PyImport_ImportModule("sys");
    if (sys)
    {
        PyObject* vk = PyImport_ImportModule("vk");
        if (vk)
        {
            PyObject_SetAttrString(sys, "stdout", vk);
            PyObject_SetAttrString(sys, "stderr", vk);
            Py_DECREF(vk);
        }
        Py_DECREF(sys);
    }

    PyLog("[Python] Initialized embedded interpreter.\n");
    initialized = true;
    return true;
#endif
}


bool PythonScriptRunner::RunFile(Application* app, const std::string& scriptPath, CmdWindow* cmdWnd)
{
#ifndef VK_ENABLE_PYTHON
    (void)scriptPath;
    return EnsureInitialized(app, cmdWnd);
#else
    if (!EnsureInitialized(app, cmdWnd))
        return false;

    // Add scripts directory to sys.path (if VK_SCRIPTS_DIR env var is set).
    {
        PyObject* sys = PyImport_ImportModule("sys");
        PyObject* path = sys ? PyObject_GetAttrString(sys, "path") : nullptr;
        if (path && PyList_Check(path))
        {
            const char* scriptsDir = std::getenv("VK_SCRIPTS_DIR");
            if (scriptsDir && scriptsDir[0])
            {
                PyObject* p = PyUnicode_FromString(scriptsDir);
                if (p)
                {
                    PyList_Append(path, p);
                    Py_DECREF(p);
                }
            }
        }
        Py_XDECREF(path);
        Py_XDECREF(sys);
    }

    PyLog("[Python] Running: " + scriptPath + "\n");

    FILE* fp = nullptr;
#if defined(_MSC_VER)
    fopen_s(&fp, scriptPath.c_str(), "rb");
#else
    fp = std::fopen(scriptPath.c_str(), "rb");
#endif

    if (!fp)
    {
        PyLog("[Python] ERROR: Could not open script file.\n");
        return false;
    }

    const int rc = PyRun_SimpleFileEx(fp, scriptPath.c_str(), 1 /*close*/);
    if (rc != 0)
    {
        if (PyErr_Occurred())
            PyErr_Print();
        PyLog("[Python] Script finished with errors.\n");
        return false;
    }

    PyLog("[Python] Done.\n");
    return true;
#endif
}


void PythonScriptRunner::Shutdown()
{
#ifdef VK_ENABLE_PYTHON
    if (initialized)
    {
        PyLog("[Python] Shutting down.\n");
        Py_Finalize();
        initialized = false;
    }
#endif
}
