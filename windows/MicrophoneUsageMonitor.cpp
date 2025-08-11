#include "MicrophoneUsageMonitor.h"
#include <iostream>
#include <psapi.h>

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

std::vector<ProcessInfo> MicrophoneUsageMonitor::GetActiveProcesses() {
    std::vector<ProcessInfo> processes;
    
    if (!m_deviceEnumerator) return processes;
    
    IMMDeviceCollection* pCollection = nullptr;
    HRESULT hr = m_deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) return processes;
    
    UINT deviceCount = 0;
    pCollection->GetCount(&deviceCount);
    
    for (UINT deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(deviceIndex, &pDevice);
        if (FAILED(hr)) continue;
        
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
                        
                        if (processId != 0 && state == AudioSessionStateActive) {
                            ProcessInfo info;
                            info.processId = processId;
                            info.processName = GetProcessName(processId);
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

void MicrophoneUsageMonitor::CheckAndReportStateChange() {
    if (!m_callback) return;
    
    std::vector<ProcessInfo> processes = GetActiveProcesses();
    bool currentState = !processes.empty();
    
    // Only report if state actually changed
    if (currentState != m_lastReportedState) {
        m_lastReportedState = currentState;
        m_callback(currentState, processes);
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