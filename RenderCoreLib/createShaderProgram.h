#include <glad/glad.h>
#include <string>
#include <iostream>
#pragma once
#include <glad/glad.h>
#include <string>

GLuint createShaderProgram(const std::string& vertexSource,
    const std::string& fragmentSource,
    const std::string& geometrySource = "");
