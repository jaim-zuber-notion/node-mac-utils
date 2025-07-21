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

// Original function returning vector (restored)
std::vector<std::string> GetAudioInputProcesses();

// New function with structured result
AudioProcessResult GetProcessesAccessingMicrophoneWithResult();