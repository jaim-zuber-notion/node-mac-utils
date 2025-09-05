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
#include <functiondiscoverykeys_devpkey.h>

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

// New function with structured result
AudioProcessResult GetProcessesAccessingMicrophoneWithResult() {
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

std::vector<std::string> GetAudioInputProcesses() {
    std::vector<std::string> results;
    std::unordered_set<std::string> seen;  // Track unique strings
    HRESULT hr = CoInitialize(nullptr);

    if (FAILED(hr)) {
        return results;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioSessionManager2* pSessionManager = nullptr;
    IAudioSessionEnumerator* pSessionEnum = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        return results;
    }

    // Get default capture (microphone) device
    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &pDevice);
    if (FAILED(hr)) {
        pEnumerator->Release();
        return results;
    }

    bool isActive = false;
    IAudioMeterInformation* pMeter = nullptr;

    // Get the Audio Meter Interface
    hr = pDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void**)&pMeter);
    if (SUCCEEDED(hr)) {
        float peakValue = 0.0f;
        pMeter->GetPeakValue(&peakValue);
        isActive = (peakValue > 0.0f);
        pMeter->Release();
    }

    if (!isActive) {
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return results;
    }

    // Get session manager
    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
    if (FAILED(hr)) {
        std::cerr << "Failed to activate IAudioSessionManager2. HRESULT: " << std::hex << hr << std::endl;
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return results;
    }

    // Get audio session enumerator
    hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
    if (FAILED(hr)) {
        std::cerr << "Failed to get IAudioSessionEnumerator. HRESULT: " << std::hex << hr << std::endl;
        pSessionManager->Release();
        pDevice->Release();
        pEnumerator->Release();
        CoUninitialize();
        return results;
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
                    results.push_back(processPath);
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

    return results;
}

// Speaker/render process detection - separate from microphone monitoring
RenderProcessResult GetRenderProcessesWithResult() {
    RenderProcessResult result;
    HRESULT hr = CoInitialize(nullptr);

    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to initialize COM";
        result.success = false;
        return result;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDeviceCollection* pCollection = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to create device enumerator";
        result.success = false;
        CoUninitialize();
        return result;
    }

    // Get ALL active render (speaker) devices
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        result.errorCode = hr;
        result.errorMessage = "Failed to enumerate audio endpoints";
        result.success = false;
        pEnumerator->Release();
        CoUninitialize();
        return result;
    }

    UINT deviceCount = 0;
    pCollection->GetCount(&deviceCount);

    // Check each active render device
    for (UINT deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(deviceIndex, &pDevice);
        if (FAILED(hr)) continue;

        // Get device name
        std::string deviceName = "Unknown Device";
        IPropertyStore* pProps = nullptr;
        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (SUCCEEDED(hr)) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);

            if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
                std::wstring wDeviceName(varName.pwszVal);
                deviceName.resize(wDeviceName.size() * 4);
                int bytesWritten = WideCharToMultiByte(CP_UTF8, 0, wDeviceName.c_str(), -1,
                                                     &deviceName[0], deviceName.size(), nullptr, nullptr);
                if (bytesWritten > 0) {
                    deviceName.resize(bytesWritten - 1);
                }
            }

            PropVariantClear(&varName);
            pProps->Release();
        }

        IAudioSessionManager2* pSessionManager = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
        if (SUCCEEDED(hr)) {
            IAudioSessionEnumerator* pSessionEnum = nullptr;
            hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
            if (SUCCEEDED(hr)) {
                int sessionCount = 0;
                pSessionEnum->GetCount(&sessionCount);

                for (int i = 0; i < sessionCount; i++) {
                    IAudioSessionControl* pSessionControl = nullptr;
                    hr = pSessionEnum->GetSession(i, &pSessionControl);
                    if (FAILED(hr)) continue;

                    IAudioSessionControl2* pSessionControl2 = nullptr;
                    hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
                    if (SUCCEEDED(hr)) {
                        DWORD processId = 0;
                        pSessionControl2->GetProcessId(&processId);

                        AudioSessionState state;
                        pSessionControl2->GetState(&state);

                        // Check if session is active and not muted
                        ISimpleAudioVolume* pVolume = nullptr;
                        bool isActiveSession = false;
                        hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVolume);
                        if (SUCCEEDED(hr)) {
                            BOOL isMuted = FALSE;
                            pVolume->GetMute(&isMuted);

                            // For render sessions, active state + not muted = active
                            isActiveSession = (state == AudioSessionStateActive && !isMuted);
                            pVolume->Release();
                        }

                        if (processId != 0 && isActiveSession) {
                            RenderProcessInfo info;
                            info.processId = processId;
                            info.processName = GetProcessExecutablePath(processId);

                            // Extract filename from path
                            size_t lastSlash = info.processName.find_last_of("\\");
                            if (lastSlash != std::string::npos) {
                                info.processName = info.processName.substr(lastSlash + 1);
                            }

                            info.deviceName = deviceName;
                            info.isActive = true;
                            result.processes.push_back(info);
                        }

                        pSessionControl2->Release();
                    }
                    pSessionControl->Release();
                }
                pSessionEnum->Release();
            }
            pSessionManager->Release();
        }

        pDevice->Release();
    }

    // Cleanup
    pCollection->Release();
    pEnumerator->Release();
    CoUninitialize();

    return result;
}