#include "stdafx.h"
#include "ShaderProgram.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::string Utf8(const std::wstring& value)
    {
        int count = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(count, ' ');
        if (count > 0)
        {
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), int(value.size()), &result[0], count, nullptr, nullptr);
        }
        return result;
    }

    GLuint Compile(GLenum type, const wchar_t* filename)
    {
        wchar_t executable[32768] = {};
        DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
        {
            std::cerr << "Cannot resolve executable path for shaders\n";
            return 0;
        }
        std::wstring path(executable, length);
        path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"Shaders\\" + filename;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            std::cerr << "Cannot open shader: " << Utf8(path) << '\n';
            return 0;
        }
        auto size = input.tellg();
        if (size <= 0 || size > 1024 * 1024)
        {
            std::cerr << "Empty or oversized shader: " << Utf8(path) << '\n';
            return 0;
        }
        std::string source(static_cast<size_t>(size), ' ');
        input.seekg(0);
        if (!input.read(&source[0], size))
        {
            std::cerr << "Cannot read shader: " << Utf8(path) << '\n';
            return 0;
        }
        if (source.compare(0, 3, "\xEF\xBB\xBF") == 0)
        {
            source.erase(0, 3);
        }
        GLuint shader = glCreateShader(type);
        const char* data = source.c_str();
        glShaderSource(shader, 1, &data, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint count = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &count);
            std::vector<char> log(count > 0 ? count : 1, 0);
            glGetShaderInfoLog(shader, GLsizei(log.size()), nullptr, log.data());
            std::cerr << "Shader compile failed: " << Utf8(path) << '\n' << log.data() << '\n';
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
} // namespace

GLuint LoadShaderProgram(const wchar_t* vertexFile, const wchar_t* fragmentFile)
{
    GLuint vertex = Compile(GL_VERTEX_SHADER, vertexFile);
    if (!vertex)
    {
        return 0;
    }
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentFile);
    if (!fragment)
    {
        glDeleteShader(vertex);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        GLint count = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &count);
        std::vector<char> log(count > 0 ? count : 1, 0);
        glGetProgramInfoLog(program, GLsizei(log.size()), nullptr, log.data());
        std::cerr << "Shader link failed: " << Utf8(vertexFile) << " + " << Utf8(fragmentFile)
                  << '\n'
                  << log.data() << '\n';
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
