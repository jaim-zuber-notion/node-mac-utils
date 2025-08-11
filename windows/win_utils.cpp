#include <napi.h>
#include <windows.h>
#include <memory>
#include <unordered_map>
#include "AudioProcessMonitor.h"
#include "MicrophoneUsageMonitor.h"

// Global storage for active event monitors
static std::unordered_map<int, AudioSessionMonitorPtr> g_activeMonitors;
static int g_nextMonitorId = 1;

// Global storage for MicrophoneUsageMonitor (matching macOS pattern)
static std::unique_ptr<MicrophoneUsageMonitor> g_micMonitor = nullptr;
static Napi::ThreadSafeFunction g_micMonitorCallback;

// Gets a list of processes that are accessing input (microphone) - original interface
Napi::Value GetRunningInputAudioProcesses(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  try {
    std::vector<std::string> processes = GetAudioInputProcesses();

    Napi::Array result = Napi::Array::New(env);
    for (size_t i = 0; i < processes.size(); i++) {
      result.Set(i, Napi::String::New(env, processes[i]));
    }

    return result;
  } catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
}

// Gets processes accessing microphone with structured result
Napi::Value GetProcessesAccessingMicrophoneWithResult(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  try {
    AudioProcessResult result = GetProcessesAccessingMicrophoneWithResult();

    // Create a JavaScript object to represent the AudioProcessResult
    Napi::Object resultObj = Napi::Object::New(env);
    if (!result.success) {
      // Set error information
      resultObj.Set("success", Napi::Boolean::New(env, false));
      resultObj.Set("error", Napi::String::New(env, result.errorMessage));
      resultObj.Set("code", Napi::Number::New(env, result.errorCode));
      resultObj.Set("domain", Napi::String::New(env, "AudioProcessMonitor"));
      resultObj.Set("processes", Napi::Array::New(env));
    } else {
      // Set success information
      resultObj.Set("success", Napi::Boolean::New(env, true));
      resultObj.Set("error", env.Null());

      // Convert processes array
      Napi::Array processesArray = Napi::Array::New(env);
      for (size_t i = 0; i < result.processes.size(); i++) {
        processesArray.Set(i, Napi::String::New(env, result.processes[i]));
      }
      resultObj.Set("processes", processesArray);
    }

    return resultObj;
  } catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
}

// Storage for JavaScript callback references
class JSSessionCallback {
public:
    Napi::ThreadSafeFunction tsfn;
    
    JSSessionCallback(Napi::Env env, Napi::Function jsCallback) {
        tsfn = Napi::ThreadSafeFunction::New(
            env,
            jsCallback,
            "SessionCallback",
            0, // Max queue size (0 = unlimited)
            1  // Number of threads that will call this
        );
    }
    
    ~JSSessionCallback() {
        if (tsfn) {
            tsfn.Release();
        }
    }
    
    void Call(const std::string& processName, bool isActive) {
        auto callback = [=](Napi::Env env, Napi::Function jsCallback) {
            Napi::Object eventObj = Napi::Object::New(env);
            eventObj.Set("processName", Napi::String::New(env, processName));
            eventObj.Set("isActive", Napi::Boolean::New(env, isActive));
            eventObj.Set("timestamp", Napi::Date::New(env, static_cast<double>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            )));
            
            jsCallback.Call({eventObj});
        };
        
        tsfn.BlockingCall(callback);
    }
};

// Enhanced callback for detailed process info
class JSEnhancedCallback {
public:
    Napi::ThreadSafeFunction tsfn;
    
    JSEnhancedCallback(Napi::Env env, Napi::Function jsCallback) {
        tsfn = Napi::ThreadSafeFunction::New(
            env,
            jsCallback,
            "EnhancedCallback",
            0, // Max queue size
            1  // Number of threads
        );
    }
    
    ~JSEnhancedCallback() {
        if (tsfn) {
            tsfn.Release();
        }
    }
    
    void Call(const ProcessSessionInfo& info) {
        auto callback = [=](Napi::Env env, Napi::Function jsCallback) {
            Napi::Object eventObj = Napi::Object::New(env);
            eventObj.Set("processName", Napi::String::New(env, info.processName));
            eventObj.Set("fullPath", Napi::String::New(env, info.fullPath));
            eventObj.Set("processId", Napi::Number::New(env, info.processId));
            eventObj.Set("deviceName", Napi::String::New(env, info.deviceName));
            eventObj.Set("isActive", Napi::Boolean::New(env, info.isActive));
            eventObj.Set("timestamp", Napi::Date::New(env, static_cast<double>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            )));
            
            jsCallback.Call({eventObj});
        };
        
        tsfn.BlockingCall(callback);
    }
};

// Global storage for callback objects
static std::unordered_map<int, std::unique_ptr<JSSessionCallback>> g_sessionCallbacks;
static std::unordered_map<int, std::unique_ptr<JSEnhancedCallback>> g_enhancedCallbacks;

// Create an event monitor with basic callback
Napi::Value CreateAudioSessionMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Expected a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        Napi::Function jsCallback = info[0].As<Napi::Function>();
        int monitorId = g_nextMonitorId++;
        
        // Create callback wrapper
        auto callbackWrapper = std::make_unique<JSSessionCallback>(env, jsCallback);
        
        // Create the native callback
        SessionStateCallback nativeCallback = [monitorId](const std::string& processName, bool isActive) {
            auto it = g_sessionCallbacks.find(monitorId);
            if (it != g_sessionCallbacks.end()) {
                it->second->Call(processName, isActive);
            }
        };
        
        // Create the monitor
        AudioSessionMonitorPtr monitor = CreateAudioSessionMonitor(nativeCallback);
        if (!monitor) {
            Napi::Error::New(env, "Failed to create audio session monitor").ThrowAsJavaScriptException();
            return env.Null();
        }
        
        // Store everything
        g_activeMonitors[monitorId] = std::move(monitor);
        g_sessionCallbacks[monitorId] = std::move(callbackWrapper);
        
        return Napi::Number::New(env, monitorId);
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

// Create an enhanced event monitor with detailed process info
Napi::Value CreateEnhancedAudioSessionMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Expected a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        Napi::Function jsCallback = info[0].As<Napi::Function>();
        int monitorId = g_nextMonitorId++;
        
        // Create callback wrapper
        auto callbackWrapper = std::make_unique<JSEnhancedCallback>(env, jsCallback);
        
        // Create the native callback
        EnhancedSessionCallback nativeCallback = [monitorId](const ProcessSessionInfo& info) {
            auto it = g_enhancedCallbacks.find(monitorId);
            if (it != g_enhancedCallbacks.end()) {
                it->second->Call(info);
            }
        };
        
        // Create the monitor
        AudioSessionMonitorPtr monitor = CreateEnhancedAudioSessionMonitor(nativeCallback);
        if (!monitor) {
            Napi::Error::New(env, "Failed to create enhanced audio session monitor").ThrowAsJavaScriptException();
            return env.Null();
        }
        
        // Store everything
        g_activeMonitors[monitorId] = std::move(monitor);
        g_enhancedCallbacks[monitorId] = std::move(callbackWrapper);
        
        return Napi::Number::New(env, monitorId);
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

// Destroy an event monitor
Napi::Value DestroyAudioSessionMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected a monitor ID").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        int monitorId = info[0].As<Napi::Number>().Int32Value();
        
        // Clean up monitor
        auto monitorIt = g_activeMonitors.find(monitorId);
        if (monitorIt != g_activeMonitors.end()) {
            DestroyAudioSessionMonitor(monitorIt->second);
            g_activeMonitors.erase(monitorIt);
        }
        
        // Clean up callbacks
        auto sessionIt = g_sessionCallbacks.find(monitorId);
        if (sessionIt != g_sessionCallbacks.end()) {
            g_sessionCallbacks.erase(sessionIt);
        }
        
        auto enhancedIt = g_enhancedCallbacks.find(monitorId);
        if (enhancedIt != g_enhancedCallbacks.end()) {
            g_enhancedCallbacks.erase(enhancedIt);
        }
        
        return Napi::Boolean::New(env, true);
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

// Start monitoring microphone usage - matching macOS interface
Napi::Value StartMonitoringMic(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Expected a callback function").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    try {
        // Stop existing monitor if running
        if (g_micMonitor) {
            g_micMonitor->StopMonitoring();
            g_micMonitor.reset();
        }
        
        if (g_micMonitorCallback) {
            g_micMonitorCallback.Release();
        }
        
        // Create thread-safe callback
        g_micMonitorCallback = Napi::ThreadSafeFunction::New(
            env,
            info[0].As<Napi::Function>(),
            "MicListener",
            0,
            1
        );
        
        // Create monitor
        g_micMonitor = std::make_unique<MicrophoneUsageMonitor>();
        
        // Start monitoring with callback matching macOS interface
        bool success = g_micMonitor->StartMonitoring([](bool microphoneActive) {
            if (!g_micMonitorCallback) return;
            
            auto callback = [=](Napi::Env env, Napi::Function jsCallback) {
                // Call JavaScript callback with (microphoneActive, error) to match macOS interface
                // On Windows, we don't have errors in the same way, so pass null for error
                jsCallback.Call({
                    Napi::Boolean::New(env, microphoneActive),
                    env.Null()  // null error to match macOS (microphoneActive, error) signature
                });
            };
            
            g_micMonitorCallback.BlockingCall(callback);
        });
        
        if (!success) {
            Napi::Error::New(env, "Failed to start microphone monitoring").ThrowAsJavaScriptException();
            return env.Null();
        }
        
        return Napi::Boolean::New(env, true);
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

// Stop monitoring microphone usage - matching macOS interface  
Napi::Value StopMonitoringMic(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    try {
        if (g_micMonitor) {
            g_micMonitor->StopMonitoring();
            g_micMonitor.reset();
        }
        
        if (g_micMonitorCallback) {
            g_micMonitorCallback.Release();
        }
        
        return env.Undefined();
        
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

// Initialize the module exports
Napi::Object Init(Napi::Env env, Napi::Object exports) {
  Napi::Value (*originalAudioProcessesFunc)(const Napi::CallbackInfo&) = GetRunningInputAudioProcesses;
  Napi::Value (*microphoneAccessFunc)(const Napi::CallbackInfo&) = GetProcessesAccessingMicrophoneWithResult;
  Napi::Value (*createMonitorFunc)(const Napi::CallbackInfo&) = CreateAudioSessionMonitor;
  Napi::Value (*createEnhancedMonitorFunc)(const Napi::CallbackInfo&) = CreateEnhancedAudioSessionMonitor;
  Napi::Value (*destroyMonitorFunc)(const Napi::CallbackInfo&) = DestroyAudioSessionMonitor;
  Napi::Value (*startMonitoringMicFunc)(const Napi::CallbackInfo&) = StartMonitoringMic;
  Napi::Value (*stopMonitoringMicFunc)(const Napi::CallbackInfo&) = StopMonitoringMic;

  // Existing polling-based functions
  exports.Set("getRunningInputAudioProcesses",
              Napi::Function::New(env, originalAudioProcessesFunc));
  exports.Set("getProcessesAccessingMicrophoneWithResult",
              Napi::Function::New(env, microphoneAccessFunc));

  // New event-driven monitoring functions
  exports.Set("createAudioSessionMonitor",
              Napi::Function::New(env, createMonitorFunc));
  exports.Set("createEnhancedAudioSessionMonitor",
              Napi::Function::New(env, createEnhancedMonitorFunc));
  exports.Set("destroyAudioSessionMonitor",
              Napi::Function::New(env, destroyMonitorFunc));

  // MicrophoneUsageMonitor functions - matching macOS interface
  exports.Set("startMonitoringMic",
              Napi::Function::New(env, startMonitoringMicFunc));
  exports.Set("stopMonitoringMic",
              Napi::Function::New(env, stopMonitoringMicFunc));

  return exports;
}

NODE_API_MODULE(win_utils, Init)