#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functional>
#include <vector>
#include <string>

// Forward declaration
class MicrophoneUsageMonitor;

// Process information for callbacks
struct ProcessInfo {
    std::string processName;
    DWORD processId;
};

// Simple callback types matching macOS pattern
typedef std::function<void(bool microphoneActive, const std::vector<ProcessInfo>& processes)> MicUsageCallback;

// Minimal monitoring class focused on session start/stop detection
class MicrophoneUsageMonitor : public IAudioSessionNotification, public IAudioSessionEvents {
private:
    LONG m_refCount;
    MicUsageCallback m_callback;
    IMMDeviceEnumerator* m_deviceEnumerator;
    std::vector<IAudioSessionManager2*> m_sessionManagers;
    bool m_isMonitoring;
    bool m_lastReportedState;
    
    void InitializeSessionMonitoring();
    void CleanupSessionMonitoring();
    std::vector<ProcessInfo> GetActiveProcesses();
    void CheckAndReportStateChange();

public:
    MicrophoneUsageMonitor();
    ~MicrophoneUsageMonitor();
    
    // Main interface - simplified
    bool StartMonitoring(MicUsageCallback callback);
    void StopMonitoring();
    
    // IUnknown implementation
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    
    // IAudioSessionNotification - for new sessions
    STDMETHODIMP OnSessionCreated(IAudioSessionControl* pNewSession) override;
    
    // IAudioSessionEvents - for session state changes
    STDMETHODIMP OnStateChanged(AudioSessionState NewState) override;
    STDMETHODIMP OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) override;
    
    // Required but unused IAudioSessionEvents methods
    STDMETHODIMP OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnIconPathChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnSimpleVolumeChanged(float, BOOL, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }
};