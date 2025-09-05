#pragma once
#include <string>
#include <vector>

struct AudioProcessResult {
    std::vector<std::string> processes;
    HRESULT errorCode;
    std::string errorMessage;
    bool success;

    AudioProcessResult() : errorCode(S_OK), success(true) {}
};

// Speaker/render process detection for Windows
struct RenderProcessInfo {
    std::string processName;
    DWORD processId;
    std::string deviceName;
    bool isActive;
};

// Original function returning vector (restored)
std::vector<std::string> GetAudioInputProcesses();

// New function with structured result
AudioProcessResult GetProcessesAccessingMicrophoneWithResult();

// Speaker/render process detection
std::vector<RenderProcessInfo> GetRenderProcesses();