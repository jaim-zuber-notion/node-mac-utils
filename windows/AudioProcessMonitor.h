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

AudioProcessResult GetAudioInputProcesses();