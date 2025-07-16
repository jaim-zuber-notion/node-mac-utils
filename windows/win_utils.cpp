#include <napi.h>
#include <windows.h>
#include "AudioProcessMonitor.h"

// Gets a list of processes that are accessing input (microphone)
Napi::Value GetRunningInputAudioProcesses(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  try {
    AudioProcessResult result = GetAudioInputProcesses();

    if (!result.success) {
      // Create a detailed error object
      Napi::Error error = Napi::Error::New(env, result.errorMessage);
      error.Set("code", Napi::Number::New(env, result.errorCode));
      error.Set("domain", Napi::String::New(env, "AudioProcessMonitor"));
      error.ThrowAsJavaScriptException();
      return env.Null();
    }

    Napi::Array resultArray = Napi::Array::New(env);
    for (size_t i = 0; i < result.processes.size(); i++) {
      resultArray.Set(i, Napi::String::New(env, result.processes[i]));
    }

    return resultArray;
  } catch (const std::exception& e) {
    Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
    return env.Null();
  }
}

// Initialize the module exports
Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "getRunningInputAudioProcesses"),
              Napi::Function::New(env, GetRunningInputAudioProcesses));

  return exports;
}

NODE_API_MODULE(win_utils, Init)