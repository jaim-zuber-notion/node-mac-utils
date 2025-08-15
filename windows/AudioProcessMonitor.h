#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>

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

// Enhanced audio activity detection with 4-tier detection system
bool HasActiveAudio(void* pDevice);

// Bluetooth device detection utility
bool IsBluetoothDevice(void* pDevice);

// Speaker/render process detection for Windows
struct RenderProcessInfo {
    std::string processName;
    unsigned long processId;
    std::string deviceName;
    bool isActive;
};

std::vector<RenderProcessInfo> GetRenderProcesses();

// Forward declarations for event-driven monitoring
class AudioSessionMonitor;

// Custom deleter for AudioSessionMonitor to work with unique_ptr
struct AudioSessionMonitorDeleter {
    void operator()(AudioSessionMonitor* monitor) const;
};

// Enhanced process information structure
struct ProcessSessionInfo {
    std::string processName;     // e.g., "zoom.exe"
    std::string fullPath;        // Full executable path
    unsigned long processId;     // Process ID
    std::string deviceName;      // Which audio device
    bool isActive;               // Session state
    
    ProcessSessionInfo() : processId(0), isActive(false) {}
};

typedef std::function<void(const ProcessSessionInfo&)> EnhancedSessionCallback;
typedef std::function<void(const std::string&, bool)> SessionStateCallback;

// Event-driven session monitoring alongside existing polling  
typedef std::unique_ptr<AudioSessionMonitor, AudioSessionMonitorDeleter> AudioSessionMonitorPtr;

AudioSessionMonitorPtr CreateAudioSessionMonitor(SessionStateCallback callback);
AudioSessionMonitorPtr CreateEnhancedAudioSessionMonitor(EnhancedSessionCallback callback);
void DestroyAudioSessionMonitor(AudioSessionMonitorPtr& monitor);