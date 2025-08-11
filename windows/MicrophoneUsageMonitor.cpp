#include "MicrophoneUsageMonitor.h"
#include "AudioProcessMonitor.h"  // For hardened HasActiveAudio logic
#include <iostream>
#include <psapi.h>
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "Ole32.lib")

// Helper function to get process name from PID
static std::string GetProcessName(DWORD processId) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) return "Unknown";
    
    WCHAR path[MAX_PATH];
    DWORD size = MAX_PATH;
    
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
        CloseHandle(hProcess);
        
        // Convert to UTF8 and extract filename
        int strSize = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        std::string fullPath(strSize, 0);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &fullPath[0], strSize, nullptr, nullptr);
        if (!fullPath.empty() && fullPath.back() == 0) {
            fullPath.pop_back();
        }
        
        // Extract just the filename
        size_t lastSlash = fullPath.find_last_of("\\");
        return (lastSlash != std::string::npos) ? fullPath.substr(lastSlash + 1) : fullPath;
    }
    
    CloseHandle(hProcess);
    return "Unknown";
}


MicrophoneUsageMonitor::MicrophoneUsageMonitor() 
    : m_refCount(1), m_deviceEnumerator(nullptr), m_isMonitoring(false), m_lastReportedState(false) {
    CoInitialize(nullptr);
}

MicrophoneUsageMonitor::~MicrophoneUsageMonitor() {
    StopMonitoring();
    CoUninitialize();
}

bool MicrophoneUsageMonitor::StartMonitoring(MicUsageCallback callback) {
    if (m_isMonitoring) {
        StopMonitoring();
    }
    
    m_callback = callback;
    
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&m_deviceEnumerator);
    if (FAILED(hr)) return false;
    
    InitializeSessionMonitoring();
    m_isMonitoring = true;
    
    // Trigger initial check
    CheckAndReportStateChange();
    
    return true;
}

void MicrophoneUsageMonitor::StopMonitoring() {
    if (!m_isMonitoring) return;
    
    CleanupSessionMonitoring();
    
    if (m_deviceEnumerator) {
        m_deviceEnumerator->Release();
        m_deviceEnumerator = nullptr;
    }
    
    m_isMonitoring = false;
}

void MicrophoneUsageMonitor::InitializeSessionMonitoring() {
    if (!m_deviceEnumerator) return;
    
    // Get all capture devices
    IMMDeviceCollection* pCollection = nullptr;
    HRESULT hr = m_deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
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
            // Register for session notifications
            pSessionManager->RegisterSessionNotification(this);
            
            // Register for existing sessions
            IAudioSessionEnumerator* pSessionEnum = nullptr;
            hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
            if (SUCCEEDED(hr)) {
                int sessionCount = 0;
                pSessionEnum->GetCount(&sessionCount);
                
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
}

void MicrophoneUsageMonitor::CleanupSessionMonitoring() {
    for (auto* pSessionManager : m_sessionManagers) {
        pSessionManager->UnregisterSessionNotification(this);
        pSessionManager->Release();
    }
    m_sessionManagers.clear();
}

bool MicrophoneUsageMonitor::HasActiveMicrophoneSessions() {
    if (!m_deviceEnumerator) return false;
    
    IMMDeviceCollection* pCollection = nullptr;
    HRESULT hr = m_deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) return false;
    
    UINT deviceCount = 0;
    pCollection->GetCount(&deviceCount);
    
    bool hasActiveSessions = false;
    
    // Check each active capture device using the hardened HasActiveAudio logic
    for (UINT deviceIndex = 0; deviceIndex < deviceCount && !hasActiveSessions; deviceIndex++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(deviceIndex, &pDevice);
        if (FAILED(hr)) continue;
        
        // Use the existing hardened detection logic from AudioProcessMonitor
        // This includes Bluetooth device handling, debouncing, and multi-tier detection
        if (HasActiveAudio(pDevice)) {
            hasActiveSessions = true;
        }
        
        pDevice->Release();
    }
    
    pCollection->Release();
    return hasActiveSessions;
}

std::vector<RenderProcessInfo> MicrophoneUsageMonitor::GetActiveRenderProcesses() {
    std::vector<RenderProcessInfo> processes;
    
    if (!m_deviceEnumerator) return processes;
    
    IMMDeviceCollection* pCollection = nullptr;
    // Key difference: eRender instead of eCapture for speaker/output devices
    HRESULT hr = m_deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) return processes;
    
    UINT deviceCount = 0;
    pCollection->GetCount(&deviceCount);
    
    // Check each active render device
    for (UINT deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(deviceIndex, &pDevice);
        if (FAILED(hr)) continue;
        
        std::string deviceName = GetDeviceName(pDevice);
        
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
                        
                        // Check if session is active and has some volume
                        ISimpleAudioVolume* pVolume = nullptr;
                        bool hasAudio = false;
                        hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pVolume);
                        if (SUCCEEDED(hr)) {
                            float sessionVolume = 0.0f;
                            pVolume->GetMasterVolume(&sessionVolume);
                            BOOL isMuted = FALSE;
                            pVolume->GetMute(&isMuted);
                            
                            // For render sessions, we want active sessions that aren't muted
                            // Volume can be low but still considered active
                            hasAudio = (state == AudioSessionStateActive && !isMuted);
                            pVolume->Release();
                        }
                        
                        if (processId != 0 && hasAudio) {
                            RenderProcessInfo info;
                            info.processId = processId;
                            info.processName = GetProcessName(processId);
                            info.deviceName = deviceName;
                            info.isActive = true;
                            processes.push_back(info);
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
    
    pCollection->Release();
    return processes;
}

std::string MicrophoneUsageMonitor::GetDeviceName(IMMDevice* pDevice) {
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
        deviceName.resize(wDeviceName.size() * 4); // Generous buffer for UTF8
        int bytesWritten = WideCharToMultiByte(CP_UTF8, 0, wDeviceName.c_str(), -1, 
                                             &deviceName[0], deviceName.size(), nullptr, nullptr);
        if (bytesWritten > 0) {
            deviceName.resize(bytesWritten - 1); // Remove null terminator
        }
    }
    
    PropVariantClear(&varName);
    pProps->Release();
    return deviceName;
}

void MicrophoneUsageMonitor::CheckAndReportStateChange() {
    if (!m_callback) return;
    
    bool currentState = HasActiveMicrophoneSessions();
    std::vector<RenderProcessInfo> renderProcesses = GetActiveRenderProcesses();
    
    // Only report if microphone state actually changed
    // Note: We always include current render processes even if mic state didn't change
    // This gives real-time updates on what's playing audio
    if (currentState != m_lastReportedState) {
        m_lastReportedState = currentState;
        m_callback(currentState, renderProcesses);
    }
}

// IUnknown implementation
STDMETHODIMP MicrophoneUsageMonitor::QueryInterface(REFIID riid, void** ppv) {
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

STDMETHODIMP_(ULONG) MicrophoneUsageMonitor::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) MicrophoneUsageMonitor::Release() {
    LONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) {
        delete this;
    }
    return ref;
}

// IAudioSessionNotification implementation
STDMETHODIMP MicrophoneUsageMonitor::OnSessionCreated(IAudioSessionControl* pNewSession) {
    if (!pNewSession || !m_callback) return S_OK;
    
    // Register for events on the new session
    pNewSession->RegisterAudioSessionNotification(this);
    
    // Check and report state change only if state actually changed
    CheckAndReportStateChange();
    
    return S_OK;
}

// IAudioSessionEvents implementation
STDMETHODIMP MicrophoneUsageMonitor::OnStateChanged(AudioSessionState NewState) {
    if (!m_callback) return S_OK;
    
    // Check and report state change only if overall microphone state changed
    CheckAndReportStateChange();
    
    return S_OK;
}

STDMETHODIMP MicrophoneUsageMonitor::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) {
    if (!m_callback) return S_OK;
    
    // Check and report state change only if overall microphone state changed
    CheckAndReportStateChange();
    
    return S_OK;
}