#include <napi.h>
#include <windows.h>
#include <memory>
#include "AudioProcessMonitor.h"
#include "MicrophoneUsageMonitor.h"

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

// Gets processes accessing speaker/output devices with structured result
Napi::Value GetProcessesAccessingSpeakerWithResult(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  try {
    AudioProcessResult result = GetProcessesAccessingSpeakerWithResult();

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

// Check if a device is a Bluetooth device (Windows-specific utility)
Napi::Value IsBluetoothDevice(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected one argument: deviceId").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "Device ID must be a string").ThrowAsJavaScriptException();
    return env.Null();
  }

  try {
    std::string deviceIdStr = info[0].As<Napi::String>().Utf8Value();
    
    bool isBluetooth = IsBluetoothDeviceById(deviceIdStr);
    return Napi::Boolean::New(env, isBluetooth);
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
        
        // Start monitoring with callback matching macOS interface exactly
        bool success = g_micMonitor->StartMonitoring([](bool microphoneActive) {
            if (!g_micMonitorCallback) return;
            
            auto callback = [=](Napi::Env env, Napi::Function jsCallback) {
                // Call JavaScript callback with (microphoneActive, error) to match macOS interface exactly
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
  Napi::Value (*speakerAccessFunc)(const Napi::CallbackInfo&) = GetProcessesAccessingSpeakerWithResult;
  Napi::Value (*bluetoothDeviceFunc)(const Napi::CallbackInfo&) = IsBluetoothDevice;
  Napi::Value (*startMonitoringMicFunc)(const Napi::CallbackInfo&) = StartMonitoringMic;
  Napi::Value (*stopMonitoringMicFunc)(const Napi::CallbackInfo&) = StopMonitoringMic;

  exports.Set("getRunningInputAudioProcesses",
              Napi::Function::New(env, originalAudioProcessesFunc));
  exports.Set("getProcessesAccessingMicrophoneWithResult",
              Napi::Function::New(env, microphoneAccessFunc));
  exports.Set("getProcessesAccessingSpeakerWithResult",
              Napi::Function::New(env, speakerAccessFunc));
  exports.Set("isBluetoothDevice",
              Napi::Function::New(env, bluetoothDeviceFunc));
  
  // MicrophoneUsageMonitor functions - matching macOS interface
  exports.Set("startMonitoringMic",
              Napi::Function::New(env, startMonitoringMicFunc));
  exports.Set("stopMonitoringMic",
              Napi::Function::New(env, stopMonitoringMicFunc));

  return exports;
}

NODE_API_MODULE(win_utils, Init)