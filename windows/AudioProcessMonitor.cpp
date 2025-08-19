// AudioProcessMonitor.cpp
//

// Include windows.h FIRST, before any other Windows headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX  // Prevent Windows from defining min/max macros
#include <windows.h>

// Now include other Windows headers
#include <psapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propkey.h>
#include <devpkey.h>

// Standard library includes
#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <chrono>
#include <unordered_map>
#include <algorithm>
#include <cctype>

// Project includes
#include "AudioProcessMonitor.h"

#pragma comment(lib, "Ole32.lib")

// Forward declaration
class AudioSessionMonitor;

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

// Helper function to get device ID for state tracking
static std::wstring GetDeviceId(IMMDevice* pDevice) {
    LPWSTR deviceId = nullptr;
    HRESULT hr = pDevice->GetId(&deviceId);
    if (FAILED(hr)) return L"";

    std::wstring id(deviceId);
    CoTaskMemFree(deviceId);
    return id;
}

// Enhanced function to check if device is Bluetooth using reliable Windows property keys
bool IsBluetoothDevice(void* pDevicePtr) {
    IMMDevice* pDevice = static_cast<IMMDevice*>(pDevicePtr);
    IPropertyStore* pProps = nullptr;
    HRESULT hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (FAILED(hr)) return false;

    bool isBluetooth = false;

    // Method 1: Check device instance ID for Bluetooth hardware patterns (most reliable)
    PROPVARIANT varInstanceId;
    PropVariantInit(&varInstanceId);
    hr = pProps->GetValue(PKEY_Device_InstanceId, &varInstanceId);
    if (SUCCEEDED(hr) && varInstanceId.vt == VT_LPWSTR) {
        std::wstring instanceId = varInstanceId.pwszVal;
        // Convert to uppercase for case-insensitive matching
        std::transform(instanceId.begin(), instanceId.end(), instanceId.begin(), ::towupper);
        // Bluetooth devices have specific patterns in their instance IDs
        isBluetooth = (instanceId.find(L"BTHENUM") != std::wstring::npos ||
                      instanceId.find(L"BTH\\") != std::wstring::npos ||
                      instanceId.find(L"BLUETOOTH") != std::wstring::npos);
    }
    PropVariantClear(&varInstanceId);

    // Method 2: Check device hardware IDs for Bluetooth vendor patterns
    if (!isBluetooth) {
        PROPVARIANT varHardwareIds;
        PropVariantInit(&varHardwareIds);
        hr = pProps->GetValue(PKEY_Device_HardwareIds, &varHardwareIds);
        if (SUCCEEDED(hr) && varHardwareIds.vt == (VT_VECTOR | VT_LPWSTR)) {
            for (ULONG i = 0; i < varHardwareIds.calpwstr.cElems; i++) {
                std::wstring hardwareId = varHardwareIds.calpwstr.pElems[i];
                std::transform(hardwareId.begin(), hardwareId.end(), hardwareId.begin(), ::towupper);
                if (hardwareId.find(L"BLUETOOTH") != std::wstring::npos ||
                    hardwareId.find(L"BTHENUM") != std::wstring::npos ||
                    hardwareId.find(L"BTH\\") != std::wstring::npos) {
                    isBluetooth = true;
                    break;
                }
            }
        }
        PropVariantClear(&varHardwareIds);
    }

    // Method 3: Check parent device ID for Bluetooth radio patterns
    if (!isBluetooth) {
        PROPVARIANT varParent;
        PropVariantInit(&varParent);
        hr = pProps->GetValue(PKEY_Device_Parent, &varParent);
        if (SUCCEEDED(hr) && varParent.vt == VT_LPWSTR) {
            std::wstring parentId = varParent.pwszVal;
            std::transform(parentId.begin(), parentId.end(), parentId.begin(), ::towupper);
            isBluetooth = (parentId.find(L"BLUETOOTH") != std::wstring::npos ||
                          parentId.find(L"BTHENUM") != std::wstring::npos);
        }
        PropVariantClear(&varParent);
    }

    // Method 4: Check device class GUID for Bluetooth audio classes
    if (!isBluetooth) {
        PROPVARIANT varClassGuid;
        PropVariantInit(&varClassGuid);
        hr = pProps->GetValue(PKEY_Device_ClassGuid, &varClassGuid);
        if (SUCCEEDED(hr) && varClassGuid.vt == VT_LPWSTR) {
            std::wstring classGuid = varClassGuid.pwszVal;
            std::transform(classGuid.begin(), classGuid.end(), classGuid.begin(), ::towupper);
            
            // Check for Bluetooth-specific device class GUIDs
            // {e0cbf06c-cd8b-4647-bb8a-263b43f0f974} - Bluetooth devices
            if (classGuid.find(L"E0CBF06C-CD8B-4647-BB8A-263B43F0F974") != std::wstring::npos) {
                isBluetooth = true;
            }
        }
        PropVariantClear(&varClassGuid);
    }

    // Method 5: Check bus type reported by the device
    if (!isBluetooth) {
        PROPVARIANT varBusType;
        PropVariantInit(&varBusType);
        hr = pProps->GetValue(PKEY_Device_BusTypeGuid, &varBusType);
        if (SUCCEEDED(hr) && varBusType.vt == VT_LPWSTR) {
            std::wstring busType = varBusType.pwszVal;
            std::transform(busType.begin(), busType.end(), busType.begin(), ::towupper);
            
            // Bluetooth bus type GUID: {2bd67d8b-8beb-48d5-87e0-6cda3428040a}
            if (busType.find(L"2BD67D8B-8BEB-48D5-87E0-6CDA3428040A") != std::wstring::npos) {
                isBluetooth = true;
            }
        }
        PropVariantClear(&varBusType);
    }

    // Method 6: Fallback to device friendly name patterns (least reliable, but catches edge cases)
    if (!isBluetooth) {
        PROPVARIANT varFriendlyName;
        PropVariantInit(&varFriendlyName);
        hr = pProps->GetValue(PKEY_DeviceInterface_FriendlyName, &varFriendlyName);
        if (FAILED(hr)) {
            // Try device description if friendly name isn't available
            hr = pProps->GetValue(PKEY_Device_DeviceDesc, &varFriendlyName);
        }
        if (SUCCEEDED(hr) && varFriendlyName.vt == VT_LPWSTR) {
            std::wstring deviceName = varFriendlyName.pwszVal;
            std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(), ::towupper);
            // More comprehensive pattern matching for Bluetooth indicators
            isBluetooth = (deviceName.find(L"BLUETOOTH") != std::wstring::npos ||
                          deviceName.find(L"HANDS-FREE") != std::wstring::npos ||
                          deviceName.find(L"A2DP") != std::wstring::npos ||
                          deviceName.find(L"HFP") != std::wstring::npos ||
                          deviceName.find(L"HSP") != std::wstring::npos ||
                          deviceName.find(L"AVRCP") != std::wstring::npos ||
                          deviceName.find(L"AIRPODS") != std::wstring::npos ||
                          deviceName.find(L"WIRELESS HEADSET") != std::wstring::npos ||
                          deviceName.find(L"BT ") != std::wstring::npos ||
                          (deviceName.find(L"WIRELESS") != std::wstring::npos &&
                           deviceName.find(L"AUDIO") != std::wstring::npos));
        }
        PropVariantClear(&varFriendlyName);
    }

    pProps->Release();
    return isBluetooth;
}

// Helper function to check if device ID is Bluetooth (wrapper for string-based API)
bool IsBluetoothDeviceById(const std::string& deviceId) {
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) return false;

    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    // Convert string to wide string
    int wideSize = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
    if (wideSize == 0) {
        pEnumerator->Release();
        CoUninitialize();
        return false;
    }
    
    std::wstring wideDeviceId(wideSize - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, &wideDeviceId[0], wideSize);

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDevice(wideDeviceId.c_str(), &pDevice);
    if (FAILED(hr)) {
        pEnumerator->Release();
        CoUninitialize();
        return false;
    }

    bool isBluetooth = IsBluetoothDevice(pDevice);

    pDevice->Release();
    pEnumerator->Release();
    CoUninitialize();

    return isBluetooth;
}

// Helper function to check session activity with standard logic (no Bluetooth-specific permissiveness)
static bool CheckSessionsForActivity(IMMDevice* pDevice) {
    IAudioSessionManager2* pSessionManager = nullptr;
    HRESULT hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
    if (FAILED(hr)) return false;

    IAudioSessionEnumerator* pSessionEnum = nullptr;
    hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
    if (FAILED(hr)) {
        pSessionManager->Release();
        return false;
    }

    int sessionCount = 0;
    pSessionEnum->GetCount(&sessionCount);

    bool hasActiveSessions = false;
    for (int i = 0; i < sessionCount; i++) {
        IAudioSessionControl* pSessionControl = nullptr;
        hr = pSessionEnum->GetSession(i, &pSessionControl);
        if (FAILED(hr)) continue;

        // Check session state
        AudioSessionState state;
        pSessionControl->GetState(&state);

        // Check session volume
        ISimpleAudioVolume* pVolume = nullptr;
        hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVolume);
        if (SUCCEEDED(hr)) {
            float sessionVolume = 0.0f;
            pVolume->GetMasterVolume(&sessionVolume);
            BOOL isMuted = FALSE;
            pVolume->GetMute(&isMuted);

            // Standard logic for all devices (no Bluetooth permissiveness)
            if (state == AudioSessionStateActive && sessionVolume > 0.0f && !isMuted) {
                hasActiveSessions = true;
            }
            pVolume->Release();
        }
        pSessionControl->Release();

        if (hasActiveSessions) break;
    }

    pSessionEnum->Release();
    pSessionManager->Release();
    return hasActiveSessions;
}

// 4-tier audio activity detection system
bool HasActiveAudio(void* pDevicePtr) {
    IMMDevice* pDevice = static_cast<IMMDevice*>(pDevicePtr);
    bool hasActiveAudio = false;
    HRESULT hr;

    // Method 1: Check peak value
    IAudioMeterInformation* pMeter = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void**)&pMeter);
    if (SUCCEEDED(hr)) {
        float peakValue = 0.0f;
        pMeter->GetPeakValue(&peakValue);
        if (peakValue > 0.0f) hasActiveAudio = true;
        pMeter->Release();
    }

    // Method 2: Check padding (buffer activity)
    if (!hasActiveAudio) {
        IAudioClient* pAudioClient = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient);
        if (SUCCEEDED(hr)) {
            UINT32 padding = 0;
            hr = pAudioClient->GetCurrentPadding(&padding);
            if (SUCCEEDED(hr) && padding > 0) hasActiveAudio = true;
            pAudioClient->Release();
        }
    }

    // Method 3: Check active sessions with standard logic
    if (!hasActiveAudio) {
        hasActiveAudio = CheckSessionsForActivity(pDevice);
    }

    // Method 4: Session enumeration fallback (basic existence check)
    if (!hasActiveAudio) {
        IAudioSessionManager2* pSessionManager = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
        if (SUCCEEDED(hr)) {
            IAudioSessionEnumerator* pSessionEnum = nullptr;
            hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
            if (SUCCEEDED(hr)) {
                int sessionCount = 0;
                pSessionEnum->GetCount(&sessionCount);
                hasActiveAudio = (sessionCount > 0);
                pSessionEnum->Release();
            }
            pSessionManager->Release();
        }
    }

    return hasActiveAudio;
}

// Enhanced function with structured result using enumeration of all devices
AudioProcessResult GetProcessesAccessingMicrophoneWithResult() {
    AudioProcessResult result;
    std::unordered_set<std::string> seen;
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

    // Get ALL active capture devices instead of just default
    hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
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

    // Check each active capture device
    for (UINT deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(deviceIndex, &pDevice);
        if (FAILED(hr)) continue;

        // Use simple activity detection (like main branch) instead of complex HasActiveAudio
        bool isPeakValueActive = false;
        IAudioMeterInformation* pMeter = nullptr;

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

        // Check sessions if there's any audio activity OR if we have active sessions (more permissive)
        IAudioSessionManager2* pSessionManager = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
        if (SUCCEEDED(hr)) {
            IAudioSessionEnumerator* pSessionEnum = nullptr;
            hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
            if (SUCCEEDED(hr)) {
                int sessionCount = 0;
                pSessionEnum->GetCount(&sessionCount);

                // More permissive: check sessions if there's audio activity OR active sessions exist
                if (isPeakValueActive || isPaddingActive || sessionCount > 0) {
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

// Enhanced function for speaker/output processes with structured result using enumeration of all devices
AudioProcessResult GetProcessesAccessingSpeakerWithResult() {
    AudioProcessResult result;
    std::unordered_set<std::string> seen;
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

    // Get ALL active render (speaker/output) devices instead of just default
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

        // Use enhanced activity detection (no Bluetooth special handling)
        if (HasActiveAudio(pDevice)) {
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
                    pSessionEnum->Release();
                }
                pSessionManager->Release();
            }
        }

        pDevice->Release();
    }

    // Cleanup
    pCollection->Release();
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