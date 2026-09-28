#pragma once
#include <cstdint>
#include <fstream>
#include <string>

// Persistent telemetry. Events are JSONL, per-presented-frame samples are CSV.
class RenderDiagnostics
{
  public:
    static RenderDiagnostics& Get();
    void Open(const std::string& mode);
    void Event(const std::string& type, const std::string& detail);
    void Frame(const std::string& row);
    void Flush();
    static std::wstring ExecutableDirectory();

  private:
    std::ofstream events_, frames_;
};
