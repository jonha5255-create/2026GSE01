#include "stdafx.h"
#include "RenderDiagnostics.h"
#include <windows.h>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace
{
    std::string Escape(const std::string& text)
    {
        std::string out;
        for (unsigned char c : text)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += char(c);
            }
            else if (c == '\n')
            {
                out += "\\n";
            }
            else if (c == '\r')
            {
                out += "\\r";
            }
            else if (c == '\t')
            {
                out += "\\t";
            }
            else if (c >= 32)
            {
                out += char(c);
            }
        }
        return out;
    }
} // namespace

RenderDiagnostics& RenderDiagnostics::Get()
{
    static RenderDiagnostics logger;
    return logger;
}

std::wstring RenderDiagnostics::ExecutableDirectory()
{
    wchar_t path[32768] = {};
    DWORD size = GetModuleFileNameW(nullptr, path, 32768);
    if (!size || size >= 32768)
    {
        return L".\\";
    }
    std::wstring full(path, size);
    return full.substr(0, full.find_last_of(L"\\/") + 1);
}

void RenderDiagnostics::Open(const std::string& mode)
{
    auto directory = ExecutableDirectory() + L"Logs";
    CreateDirectoryW(directory.c_str(), nullptr);
    SYSTEMTIME time;
    GetSystemTime(&time);
    std::wostringstream name;
    name << directory << L"\\render-" << time.wYear << L'-' << std::setfill(L'0') << std::setw(2)
         << time.wMonth << std::setw(2) << time.wDay << L'-' << std::setw(2) << time.wHour
         << std::setw(2) << time.wMinute << std::setw(2) << time.wSecond << L'-'
         << GetCurrentProcessId();
    events_.open(name.str() + L".jsonl", std::ios::out | std::ios::trunc);
    frames_.open(name.str() + L".csv", std::ios::out | std::ios::trunc);
    if (!events_ || !frames_)
    {
        std::cerr << "[PERF] Cannot open persistent renderer logs; console remains available.\n";
    }
    frames_ << "frame,elapsed_s,frame_ms,render_cpu_ms,update_cpu_ms,draw_calls,scene_draws,post_"
               "draws,ui_draws,objects,triangle_instances,instance_upload_bytes,mesh_upload_bytes,"
               "cache_hits,cache_misses,mesh_generations,disk_loads,disk_rejects,disk_writes,cache_"
               "evictions,batch_splits\n";
    Event("session",
          "schema=1; renderer=ordered-triangle-instancing-v1; mode=" + mode
#ifdef _DEBUG
              + "; build=Debug"
#else
              + "; build=Release"
#endif
    );
    std::cout << "[PERF] Persistent logs: Logs/render-*.csv and *.jsonl (next to executable)\n";
}

void RenderDiagnostics::Event(const std::string& type, const std::string& detail)
{
    events_ << "{\"tick_ms\":" << GetTickCount64() << ",\"event\":\"" << Escape(type)
            << "\",\"detail\":\"" << Escape(detail) << "\"}\n";
}

void RenderDiagnostics::Frame(const std::string& row)
{
    frames_ << row << '\n';
}

void RenderDiagnostics::Flush()
{
    events_.flush();
    frames_.flush();
}
