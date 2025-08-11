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

// Enhanced state caching for Bluetooth device debouncing and power management
struct BluetoothDeviceState {
    bool lastActiveState;
    bool lastReportedState;  // What we last reported to the caller
    std::chrono::steady_clock::time_point lastStateChange;
    std::chrono::steady_clock::time_point lastActivityTime;  // Last time any activity was detected
    std::chrono::steady_clock::time_point lastReportTime;    // Last time we reported a state
    int consecutiveActiveChecks;
    int consecutiveInactiveChecks;
    int rapidStateChangeCount;  // Track rapid flapping
    std::chrono::steady_clock::time_point rapidStateChangeWindow;

    BluetoothDeviceState() :
        lastActiveState(false),
        lastReportedState(false),
        lastStateChange(std::chrono::steady_clock::now()),
        lastActivityTime(std::chrono::steady_clock::now()),
        lastReportTime(std::chrono::steady_clock::now()),
        consecutiveActiveChecks(0),
        consecutiveInactiveChecks(0),
        rapidStateChangeCount(0),
        rapidStateChangeWindow(std::chrono::steady_clock::now()) {}
};

static std::unordered_map<std::wstring, BluetoothDeviceState> bluetoothStateCache;

// Enhanced debouncing constants for power management scenarios
static const int BLUETOOTH_DEBOUNCE_MS = 3000; // Increased to 3 seconds for power management
static const int BLUETOOTH_ACTIVE_HOLD_MS = 5000; // Hold active state for 5 seconds after last activity
static const int REQUIRED_CONSECUTIVE_ACTIVE_CHECKS = 2; // Reduced for faster response
static const int REQUIRED_CONSECUTIVE_INACTIVE_CHECKS = 4; // More checks needed to go inactive
static const int RAPID_CHANGE_WINDOW_MS = 10000; // 10 second window for flapping detection
static const int MAX_RAPID_CHANGES = 5; // Max state changes in window before extending debounce
static const int EXTENDED_DEBOUNCE_MS = 8000; // Extended debounce for flapping devices

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
        }return result;
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
static bool IsBluetoothDevice(IMMDevice* pDevice) {
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
                }}
        }PropVariantClear(&varHardwareIds);
    }

    // Method 3: Check container ID against Bluetooth service class (when available)
    if (!isBluetooth) {
        PROPVARIANT varContainerId;
        PropVariantInit(&varContainerId);
        hr = pProps->GetValue(PKEY_Device_ContainerId, &varContainerId);
        if (SUCCEEDED(hr) && varContainerId.vt == VT_CLSID) {
            // If we have a container ID, we could potentially check if it's associated 
            // with a Bluetooth service, but this requires additional APIs
            // For now, this is a placeholder for future enhancement
        }PropVariantClear(&varContainerId);
    }

    // Method 4: Check parent device ID for Bluetooth radio patterns
    if (!isBluetooth) {
        PROPVARIANT varParent;
        PropVariantInit(&varParent);
        hr = pProps->GetValue(PKEY_Device_Parent, &varParent);
        if (SUCCEEDED(hr) && varParent.vt == VT_LPWSTR) {
            std::wstring parentId = varParent.pwszVal;
            std::transform(parentId.begin(), parentId.end(), parentId.begin(), ::towupper);
            isBluetooth = (parentId.find(L"BLUETOOTH") != std::wstring::npos ||
                          parentId.find(L"BTHENUM") != std::wstring::npos);
        }PropVariantClear(&varParent);
    }

    // Method 5: Check device class GUID for Bluetooth audio classes
    if (!isBluetooth) {
        PROPVARIANT varClassGuid;
        PropVariantInit(&varClassGuid);
        hr = pProps->GetValue(PKEY_Device_ClassGuid, &varClassGuid);
        if (SUCCEEDED(hr) && varClassGuid.vt == VT_LPWSTR) {
            std::wstring classGuid = varClassGuid.pwszVal;
            std::transform(classGuid.begin(), classGuid.end(), classGuid.begin(), ::towupper);
            
            // Check for Bluetooth-specific device class GUIDs
            // {e0cbf06c-cd8b-4647-bb8a-263b43f0f974} - Bluetooth devices
            // {4d36e96c-e325-11ce-bfc1-08002be10318} - Sound, video and game controllers (may include BT audio)
            if (classGuid.find(L"E0CBF06C-CD8B-4647-BB8A-263B43F0F974") != std::wstring::npos) {
                isBluetooth = true;
            }}
        PropVariantClear(&varClassGuid);
    }

    // Method 6: Check bus type reported by the device
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
            }}
        PropVariantClear(&varBusType);
    }

    // Method 7: Fallback to device friendly name patterns (least reliable, but catches edge cases)
    if (!isBluetooth) {
        PROPVARIANT varFriendlyName;
        PropVariantInit(&varFriendlyName);
        hr = pProps->GetValue(PKEY_DeviceInterface_FriendlyName, &varFriendlyName);
        if (FAILED(hr)) {
            // Try device description if friendly name isn't available
            hr = pProps->GetValue(PKEY_Device_DeviceDesc, &varFriendlyName);
        }if (SUCCEEDED(hr) && varFriendlyName.vt == VT_LPWSTR) {
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
        }PropVariantClear(&varFriendlyName);
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
                }// Also accept sessions that are active even with zero volume if not muted
                // (some Bluetooth drivers report zero volume initially)
                else if (state == AudioSessionStateActive && !isMuted) {
                    hasActiveSessions = true;
                }} else {
                // Standard logic for non-Bluetooth devices
                if (state == AudioSessionStateActive && sessionVolume > 0.0f && !isMuted) {
                    hasActiveSessions = true;
                }}
            pVolume->Release();
        }pSessionControl->Release();

        if (hasActiveSessions) break;
    }

    pSessionEnum->Release();
    pSessionManager->Release();
    return hasActiveSessions;
}

// Enhanced function to check if device has active audio with debouncing
bool HasActiveAudio(IMMDevice* pDevice) {
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

    // Method 4: Enhanced Bluetooth-specific debouncing and power management
    if (isBluetooth && !deviceId.empty()) {
        auto& state = bluetoothStateCache[deviceId];
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastChange = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastStateChange).count();
        auto timeSinceLastActivity = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastActivityTime).count();
        auto timeSinceRapidWindow = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.rapidStateChangeWindow).count();

        // Reset rapid change counter if outside window
        if (timeSinceRapidWindow > RAPID_CHANGE_WINDOW_MS) {
            state.rapidStateChangeCount = 0;
            state.rapidStateChangeWindow = now;
        }

        // Update activity timestamp if any activity detected
        if (hasActiveAudio) {
            state.lastActivityTime = now;
        }

        // Determine current effective debounce time (extended if device is flapping)
        int effectiveDebounceMs = (state.rapidStateChangeCount >= MAX_RAPID_CHANGES) ?
                                 EXTENDED_DEBOUNCE_MS : BLUETOOTH_DEBOUNCE_MS;

        if (hasActiveAudio != state.lastActiveState) {
            // State change detected - increment rapid change counter
            state.rapidStateChangeCount++;
            if (hasActiveAudio) {
                // Going from inactive to active
                state.consecutiveActiveChecks++;
                state.consecutiveInactiveChecks = 0;
                // Require fewer consecutive checks for faster response to genuine activity
                if (state.consecutiveActiveChecks >= REQUIRED_CONSECUTIVE_ACTIVE_CHECKS) {
                    state.lastActiveState = true;
                    state.lastReportedState = true;
                    state.lastStateChange = now;
                    state.lastReportTime = now;
                    return true;
                }// Still building confidence - return last reported state during probation
                return state.lastReportedState;
            } else {
                // Going from active to inactive
                state.consecutiveInactiveChecks++;
                state.consecutiveActiveChecks = 0;
                // Apply power management holdoff - stay active if recent activity
                if (timeSinceLastActivity < BLUETOOTH_ACTIVE_HOLD_MS) {
                    return true; // Keep reporting active due to recent activity
                }    // Apply debouncing with flapping detection
                if (timeSinceLastChange < effectiveDebounceMs) {
                    return state.lastReportedState; // Stay in previous reported state during debounce
                }    // Require more consecutive inactive checks to confirm device is truly idle
                if (state.consecutiveInactiveChecks >= REQUIRED_CONSECUTIVE_INACTIVE_CHECKS) {
                    state.lastActiveState = false;
                    state.lastReportedState = false;
                    state.lastStateChange = now;
                    state.lastReportTime = now;
                    return false;
                }    // Still building confidence for inactive state
                return state.lastReportedState;
            }} else {
            // State consistent with previous check
            if (hasActiveAudio) {
                // Consistently active - reset inactive counter
                state.consecutiveInactiveChecks = 0;
                state.consecutiveActiveChecks = std::min(state.consecutiveActiveChecks + 1, REQUIRED_CONSECUTIVE_ACTIVE_CHECKS + 1);
                
                // Ensure we report active if consistently seeing activity
                if (state.consecutiveActiveChecks >= REQUIRED_CONSECUTIVE_ACTIVE_CHECKS && !state.lastReportedState) {
                    state.lastReportedState = true;
                    state.lastReportTime = now;
                }return state.lastReportedState;
            } else {
                // Consistently inactive - but check power management holdoff
                if (timeSinceLastActivity < BLUETOOTH_ACTIVE_HOLD_MS) {
                    return true; // Keep active due to recent activity
                }    state.consecutiveActiveChecks = 0;
                state.consecutiveInactiveChecks = std::min(state.consecutiveInactiveChecks + 1, REQUIRED_CONSECUTIVE_INACTIVE_CHECKS + 1);
                
                return state.lastReportedState;
            }}
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
                                }            }                pSessionControl2->Release();
                        }        pSessionControl->Release();
                    }    pSessionEnum->Release();
                }pSessionManager->Release();
            }}

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
                        }    }
                    pSessionControl2->Release();
                }pSessionControl->Release();
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

// Event-driven session monitoring implementation
class AudioSessionMonitor : public IAudioSessionNotification, public IAudioSessionEvents {
private:
    LONG m_refCount;
    SessionStateCallback m_callback;
    EnhancedSessionCallback m_enhancedCallback;
    std::vector<IAudioSessionManager2*> m_sessionManagers;
    std::unordered_map<std::string, bool> m_sessionStates; // Track session states by device+process
    std::unordered_map<DWORD, std::string> m_processCache; // Cache process names by PID
    IMMDeviceEnumerator* m_deviceEnumerator;
    bool m_initialized;

    // Helper to generate unique session key
    std::string GetSessionKey(IMMDevice* device, DWORD processId) {
        LPWSTR deviceId = nullptr;
        device->GetId(&deviceId);
        std::wstring wDeviceId(deviceId ? deviceId : L"unknown");
        CoTaskMemFree(deviceId);
        
        std::string sessionKey;
        sessionKey.resize(wDeviceId.size());
        WideCharToMultiByte(CP_UTF8, 0, wDeviceId.c_str(), -1, &sessionKey[0], sessionKey.size(), nullptr, nullptr);
        sessionKey += "_" + std::to_string(processId);
        return sessionKey;
    }

    // Helper to get device name
    std::string GetDeviceName(IMMDevice* pDevice) {
        if (!pDevice) return "Unknown Device";
        
        IPropertyStore* pProps = nullptr;
        HRESULT hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
        if (FAILED(hr)) return "Unknown Device";
        
        PROPVARIANT varName;
        PropVariantInit(&varName);
        hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
        
        std::string deviceName = "Unknown Device";
        if (SUCCEEDED(hr) && varName.vt == VT_LPWSTR) {
            std::wstring wDeviceName(varName.pwszVal);
            deviceName.resize(wDeviceName.size());
            WideCharToMultiByte(CP_UTF8, 0, wDeviceName.c_str(), -1, &deviceName[0], deviceName.size(), nullptr, nullptr);
        }
        
        PropVariantClear(&varName);
        pProps->Release();
        return deviceName;
    }

public:
    AudioSessionMonitor(SessionStateCallback callback) 
        : m_refCount(1), m_callback(callback), m_enhancedCallback(nullptr), m_deviceEnumerator(nullptr), m_initialized(false) {
        CoInitialize(nullptr);
        Initialize();
    }

    AudioSessionMonitor(EnhancedSessionCallback enhancedCallback) 
        : m_refCount(1), m_callback(nullptr), m_enhancedCallback(enhancedCallback), m_deviceEnumerator(nullptr), m_initialized(false) {
        CoInitialize(nullptr);
        Initialize();
    }

    ~AudioSessionMonitor() {
        Cleanup();
        CoUninitialize();
    }

    // IUnknown implementation
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == __uuidof(IAudioSessionNotification)) {
            *ppv = static_cast<IAudioSessionNotification*>(this);
        } else if (riid == __uuidof(IAudioSessionEvents)) {
            *ppv = static_cast<IAudioSessionEvents*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() {
        return InterlockedIncrement(&m_refCount);
    }

    STDMETHODIMP_(ULONG) Release() {
        LONG ref = InterlockedDecrement(&m_refCount);
        if (ref == 0) {
            delete this;
        }
        return ref;
    }

    // IAudioSessionNotification implementation
    STDMETHODIMP OnSessionCreated(IAudioSessionControl* pNewSession) {
        if (!pNewSession) return S_OK;

        // Register for session events
        pNewSession->RegisterAudioSessionNotification(this);
        
        // Get session details for immediate processing
        IAudioSessionControl2* pSessionControl2 = nullptr;
        HRESULT hr = pNewSession->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
        if (SUCCEEDED(hr)) {
            DWORD processId = 0;
            pSessionControl2->GetProcessId(&processId);
            
            AudioSessionState state;
            pSessionControl2->GetState(&state);
            
            if (processId != 0 && state == AudioSessionStateActive) {
                std::string processPath = GetProcessExecutablePath(processId);
                if (!processPath.empty()) {
                    std::string filename = processPath.substr(processPath.find_last_of("\\") + 1);
                    
                    // Cache the process name for this PID
                    m_processCache[processId] = filename;
                    
                    // Call appropriate callback
                    if (m_enhancedCallback) {
                        // Provide detailed process information
                        ProcessSessionInfo info;
                        info.processName = filename;
                        info.fullPath = processPath;
                        info.processId = processId;
                        info.isActive = true;
                        // Note: We don't have device context in session creation event
                        info.deviceName = "Capture Device";
                        
                        m_enhancedCallback(info);
                    } else if (m_callback) {
                        // Legacy callback
                        m_callback(filename, true);
                    }
                }
            }
            
            pSessionControl2->Release();
        }
        
        return S_OK;
    }

    // IAudioSessionEvents implementation  
    STDMETHODIMP OnStateChanged(AudioSessionState NewState) {
        // This will be called for any session state change
        // We can use this for immediate detection of session deactivation
        
        if (NewState == AudioSessionStateInactive && m_callback) {
            // Session became inactive - this is immediate call-end detection
            // We don't have process info here, so we trigger a general state check
            m_callback("", false); // Empty string signals state change, not specific process
        }
        
        return S_OK;
    }

    // Other IAudioSessionEvents methods (required but not used for our purpose)
    STDMETHODIMP OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext) { return S_OK; }
    STDMETHODIMP OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext) { return S_OK; }
    STDMETHODIMP OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext) { return S_OK; }
    STDMETHODIMP OnChannelVolumeChanged(DWORD ChannelCount, float NewChannelVolumes[], DWORD ChannelIndex, LPCGUID EventContext) { return S_OK; }
    STDMETHODIMP OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext) { return S_OK; }
    STDMETHODIMP OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) { 
        // Session explicitly disconnected - immediate call-end signal
        if (m_callback) {
            m_callback("", false); // Signal session disconnection
        }
        return S_OK; 
    }

private:
    void Initialize() {
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, 
                                    __uuidof(IMMDeviceEnumerator), (void**)&m_deviceEnumerator);
        if (FAILED(hr)) return;

        // Enumerate all capture devices and register for session notifications
        IMMDeviceCollection* pCollection = nullptr;
        hr = m_deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
        if (FAILED(hr)) return;

        UINT deviceCount = 0;
        pCollection->GetCount(&deviceCount);

        for (UINT i = 0; i < deviceCount; i++) {
            IMMDevice* pDevice = nullptr;
            hr = pCollection->Item(i, &pDevice);
            if (FAILED(hr)) continue;

            IAudioSessionManager2* pSessionManager = nullptr;
            hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&pSessionManager);
            if (SUCCEEDED(hr)) {
                // Register for new session notifications
                pSessionManager->RegisterSessionNotification(this);
                
                // CRITICAL: Enumerate existing sessions to activate notification system
                IAudioSessionEnumerator* pSessionEnum = nullptr;
                hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
                if (SUCCEEDED(hr)) {
                    int sessionCount = 0;
                    pSessionEnum->GetCount(&sessionCount); // This activates notifications!
                    
                    // Also register for events on existing sessions
                    for (int j = 0; j < sessionCount; j++) {
                        IAudioSessionControl* pSessionControl = nullptr;
                        hr = pSessionEnum->GetSession(j, &pSessionControl);
                        if (SUCCEEDED(hr)) {
                            pSessionControl->RegisterAudioSessionNotification(this);
                            pSessionControl->Release();
                        }
                    }
                    
                    pSessionEnum->Release();
                }
                
                m_sessionManagers.push_back(pSessionManager);
            }
            
            pDevice->Release();
        }
        
        pCollection->Release();
        m_initialized = true;
    }

    void Cleanup() {
        if (!m_initialized) return;
        
        // Unregister from all session managers
        for (auto* pSessionManager : m_sessionManagers) {
            pSessionManager->UnregisterSessionNotification(this);
            pSessionManager->Release();
        }
        m_sessionManagers.clear();
        
        if (m_deviceEnumerator) {
            m_deviceEnumerator->Release();
            m_deviceEnumerator = nullptr;
        }
        
        m_initialized = false;
    }
};

// Implementation of custom deleter
void AudioSessionMonitorDeleter::operator()(AudioSessionMonitor* monitor) const {
    delete monitor;
}

// Factory functions
AudioSessionMonitorPtr CreateAudioSessionMonitor(SessionStateCallback callback) {
    return AudioSessionMonitorPtr(new AudioSessionMonitor(callback));
}

AudioSessionMonitorPtr CreateEnhancedAudioSessionMonitor(EnhancedSessionCallback callback) {
    return AudioSessionMonitorPtr(new AudioSessionMonitor(callback));
}

void DestroyAudioSessionMonitor(AudioSessionMonitorPtr& monitor) {
    monitor.reset();
}