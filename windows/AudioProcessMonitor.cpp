// AudioProcessMonitor.cpp
//

// Include windows.h FIRST, before any other Windows headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

// Project includes
#include "AudioProcessMonitor.h"

#pragma comment(lib, "Ole32.lib")

// State caching for Bluetooth device debouncing
struct BluetoothDeviceState {
    bool lastActiveState;
    std::chrono::steady_clock::time_point lastStateChange;
    int consecutiveActiveChecks;

    BluetoothDeviceState() : lastActiveState(false), lastStateChange(std::chrono::steady_clock::now()), consecutiveActiveChecks(0) {}
};

static std::unordered_map<std::wstring, BluetoothDeviceState> bluetoothStateCache;
static const int BLUETOOTH_DEBOUNCE_MS = 2000; // 2 second debounce
static const int REQUIRED_CONSECUTIVE_CHECKS = 3;

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

// Enhanced function to check if device is Bluetooth using multiple property keys
static bool IsBluetoothDevice(IMMDevice* pDevice) {
    IPropertyStore* pProps = nullptr;
    HRESULT hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    if (FAILED(hr)) return false;

    bool isBluetooth = false;

    // Method 1: Check for Bluetooth device address (most reliable)
    PROPVARIANT varBluetoothAddr;
    PropVariantInit(&varBluetoothAddr);

    // Try PKEY_Device_Bluetooth_DeviceAddress (this might not be available in all contexts)
    PROPERTYKEY bluetoothAddrKey = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 1 };
    hr = pProps->GetValue(bluetoothAddrKey, &varBluetoothAddr);
    if (SUCCEEDED(hr) && (varBluetoothAddr.vt == VT_LPWSTR || varBluetoothAddr.vt == VT_BSTR)) {
        isBluetooth = true;
    }
    PropVariantClear(&varBluetoothAddr);

    // Method 2: Check device instance ID for Bluetooth patterns
    if (!isBluetooth) {
        PROPVARIANT varInstanceId;
        PropVariantInit(&varInstanceId);
        hr = pProps->GetValue(PKEY_Device_InstanceId, &varInstanceId);
        if (SUCCEEDED(hr) && varInstanceId.vt == VT_LPWSTR) {
            std::wstring instanceId = varInstanceId.pwszVal;
            // Bluetooth devices often have BTHENUM in their instance ID
            isBluetooth = (instanceId.find(L"BTHENUM") != std::wstring::npos ||
                          instanceId.find(L"BTH\\") != std::wstring::npos);
        }
        PropVariantClear(&varInstanceId);
    }

    // Method 3: Check device friendly name for Bluetooth indicators (fallback)
    if (!isBluetooth) {
        PROPVARIANT varFriendlyName;
        PropVariantInit(&varFriendlyName);
        hr = pProps->GetValue(PKEY_DeviceInterface_FriendlyName, &varFriendlyName);
        if (SUCCEEDED(hr) && varFriendlyName.vt == VT_LPWSTR) {
            std::wstring friendlyName = varFriendlyName.pwszVal;
            isBluetooth = (friendlyName.find(L"Bluetooth") != std::wstring::npos ||
                          friendlyName.find(L"Wireless") != std::wstring::npos ||
                          friendlyName.find(L"BT ") != std::wstring::npos ||
                          friendlyName.find(L"Hands-Free") != std::wstring::npos ||
                          friendlyName.find(L"A2DP") != std::wstring::npos);
        }
        PropVariantClear(&varFriendlyName);
    }

    // Method 4: Check device description as final fallback
    if (!isBluetooth) {
        PROPVARIANT varDeviceDesc;
        PropVariantInit(&varDeviceDesc);
        hr = pProps->GetValue(PKEY_Device_DeviceDesc, &varDeviceDesc);
        if (SUCCEEDED(hr) && varDeviceDesc.vt == VT_LPWSTR) {
            std::wstring deviceName = varDeviceDesc.pwszVal;
            isBluetooth = (deviceName.find(L"Bluetooth") != std::wstring::npos ||
                          deviceName.find(L"Wireless") != std::wstring::npos);
        }
        PropVariantClear(&varDeviceDesc);
    }

    pProps->Release();
    return isBluetooth;
}

// Helper function to check if device has any sessions
static bool HasAnySessions(IMMDevice* pDevice) {
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

    pSessionEnum->Release();
    pSessionManager->Release();

    return sessionCount > 0;
}

// Helper function to check session activity with Bluetooth-specific logic
static bool CheckSessionsForActivity(IMMDevice* pDevice, bool isBluetooth = false) {
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

            if (isBluetooth) {
                // For Bluetooth devices, be more permissive but still apply some filtering
                // Accept sessions that are active and have some volume (even very low) and not muted
                if (state == AudioSessionStateActive && sessionVolume > 0.001f && !isMuted) {
                    hasActiveSessions = true;
                }
                // Also accept sessions that are active even with zero volume if not muted
                // (some Bluetooth drivers report zero volume initially)
                else if (state == AudioSessionStateActive && !isMuted) {
                    hasActiveSessions = true;
                }
            } else {
                // Standard logic for non-Bluetooth devices
                if (state == AudioSessionStateActive && sessionVolume > 0.0f && !isMuted) {
                    hasActiveSessions = true;
                }
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

// Enhanced function to check if device has active audio with debouncing
static bool HasActiveAudio(IMMDevice* pDevice) {
    bool isBluetooth = IsBluetoothDevice(pDevice);
    std::wstring deviceId = GetDeviceId(pDevice);
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

    // Method 3: Check active sessions with device-specific logic
    if (!hasActiveAudio) {
        hasActiveAudio = CheckSessionsForActivity(pDevice, isBluetooth);
    }

    // Method 4: Bluetooth-specific debouncing and state management
    if (isBluetooth && !deviceId.empty()) {
        auto& state = bluetoothStateCache[deviceId];
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastChange = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastStateChange).count();

        if (hasActiveAudio != state.lastActiveState) {
            // State change detected
            if (hasActiveAudio) {
                state.consecutiveActiveChecks++;
                // Require multiple consecutive active checks before considering it truly active
                if (state.consecutiveActiveChecks >= REQUIRED_CONSECUTIVE_CHECKS) {
                    state.lastActiveState = true;
                    state.lastStateChange = now;
                    return true;
                }
                return false; // Still in probation period
            } else {
                // Going from active to inactive - apply debouncing
                if (timeSinceLastChange < BLUETOOTH_DEBOUNCE_MS) {
                    return state.lastActiveState; // Stay in previous state during debounce period
                }
                state.lastActiveState = false;
                state.consecutiveActiveChecks = 0;
                state.lastStateChange = now;
                return false;
            }
        } else {
            // State consistent - reset consecutive checks if inactive
            if (!hasActiveAudio) {
                state.consecutiveActiveChecks = 0;
            }
            return state.lastActiveState;
        }
    }

    return hasActiveAudio;
}

// New function with structured result using enumeration of all devices
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

        // Use enhanced activity detection
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
    IMMDeviceCollection* pCollection = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        return results;
    }

    // Get ALL active capture devices instead of just default
    hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        pEnumerator->Release();
        return results;
    }

    UINT deviceCount = 0;
    pCollection->GetCount(&deviceCount);

    // Check each active capture device
    for (UINT deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(deviceIndex, &pDevice);
        if (FAILED(hr)) continue;

        // Use enhanced activity detection
        if (HasActiveAudio(pDevice)) {
            // Get session manager
            IAudioSessionManager2* pSessionManager = nullptr;
            hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
            if (FAILED(hr)) {
                std::cerr << "Failed to activate IAudioSessionManager2. HRESULT: " << std::hex << hr << std::endl;
                pDevice->Release();
                continue;
            }

            // Get audio session enumerator
            IAudioSessionEnumerator* pSessionEnum = nullptr;
            hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
            if (FAILED(hr)) {
                std::cerr << "Failed to get IAudioSessionEnumerator. HRESULT: " << std::hex << hr << std::endl;
                pSessionManager->Release();
                pDevice->Release();
                continue;
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

            // Cleanup for this device
            pSessionEnum->Release();
            pSessionManager->Release();
        }

        pDevice->Release();
    }

    // Cleanup
    pCollection->Release();
    pEnumerator->Release();
    CoUninitialize();

    return results;
}