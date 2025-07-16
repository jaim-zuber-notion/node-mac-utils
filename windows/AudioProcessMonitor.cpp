// AudioProcessMonitor.cpp
//

#include <windows.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <iostream>
#include <psapi.h>
#include <string>
#include <vector>
#include "AudioProcessMonitor.h"
#include <Audioclient.h>
#include <unordered_set>

#pragma comment(lib, "Ole32.lib")

// Function to get process executable path from PID
static std::string GetProcessExecutablePath(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID);
    if (!hProcess) return "Unknown";

    WCHAR path[MAX_PATH];
    DWORD size = MAX_PATH;
    
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
        CloseHandle(hProcess);
        // Convert wide string to regular string
        int strSize = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        std::string result(strSize, 0);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &result[0], strSize, nullptr, nullptr);
        if (!result.empty() && result.back() == 0) {
            result.pop_back();
        }
        return result;
    }

    CloseHandle(hProcess);
    return "Unknown";
}

AudioProcessResult GetAudioInputProcesses() {
    AudioProcessResult result;
    std::unordered_set<std::string> seen;  // Track unique strings
    HRESULT hr = CoInitialize(nullptr);

    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to initialize COM";
        result.success = false;
        return result;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioSessionManager2* pSessionManager = nullptr;
    IAudioSessionEnumerator* pSessionEnum = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to create device enumerator";
        result.success = false;
        CoUninitialize();
        return result;
    }

    // Get default capture (microphone) device
    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &pDevice);
    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to get default audio endpoint";
        result.success = false;
        pEnumerator->Release();
        CoUninitialize();
        return result;
    }

    bool isPeakValueActive = false;
    IAudioMeterInformation* pMeter = nullptr;

    // Get the Audio Meter Interface
    hr = pDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void**)&pMeter);
    if (SUCCEEDED(hr)) {
        float peakValue = 0.0f;
        pMeter->GetPeakValue(&peakValue);
        isPeakValueActive = (peakValue > 0.0f);
        pMeter->Release();
    }

    bool isPaddingActive = false;
    IAudioClient* pAudioClient = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient);
    if (SUCCEEDED(hr)) {
        UINT32 padding = 0;
        hr = pAudioClient->GetCurrentPadding(&padding);
        isPaddingActive = SUCCEEDED(hr) && padding > 0;
        pAudioClient->Release();
    }

    if (!isPeakValueActive && !isPaddingActive) {
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        // This is not an error - just no active audio
        result.processes = std::vector<std::string>();
        return result;
    }

    // Get session manager
    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to activate IAudioSessionManager2";
        result.success = false;
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return result;
    }

    // Get audio session enumerator
    hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to get IAudioSessionEnumerator";
        result.success = false;
        pSessionManager->Release();
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return result;
    }

    int sessionCount = 0;
    pSessionEnum->GetCount(&sessionCount);

    for (int i = 0; i < sessionCount; i++) {
        IAudioSessionControl* pSessionControl = nullptr;
        hr = pSessionEnum->GetSession(i, &pSessionControl);
        if (FAILED(hr)) continue;

        IAudioSessionControl2* pSessionControl2 = nullptr;
        hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
        if (SUCCEEDED(hr)) {
            DWORD processID = 0;
            pSessionControl2->GetProcessId(&processID);

            AudioSessionState state;
            pSessionControl2->GetState(&state);

            if (processID != 0 && state == AudioSessionStateActive) {
                std::string processPath = GetProcessExecutablePath(processID);
                
                // Only insert if not already seen
                if (seen.insert(processPath).second) {
                    result.processes.push_back(processPath);
                }
            }
            pSessionControl2->Release();
        }
        pSessionControl->Release();
    }

    // Cleanup
    pSessionEnum->Release();
    pSessionManager->Release();
    pDevice->Release();
    pEnumerator->Release();
    CoUninitialize();

    return result;
}