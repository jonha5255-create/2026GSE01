#pragma once
#include "Dependencies/glew.h"

// Loads UTF-8 GLSL from Shaders beside the executable, independent of CWD.
// On failure logs the filename and compiler/linker diagnostics, returning zero.
GLuint LoadShaderProgram(const wchar_t* vertexFile, const wchar_t* fragmentFile);
