#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import "AudioProcessMonitor.h"
#import "MicrophoneUsageMonitor.h"
#include <napi.h>

static MicrophoneUsageMonitor *monitor = nil;
static Napi::ThreadSafeFunction ts_fn;

// Takes the output of BrowserWindow.getNativeWindowHandle
// (which is a NSView* to the contentView of the window),
// finds the associated window and calls `makeKeyAndOrderFront`
// on it so that it's visible and focused without activating
// its application.
void MakeKeyAndOrderFront(const Napi::CallbackInfo &info) {
  NSView **contentViewPointer =
      reinterpret_cast<NSView **>(info[0].As<Napi::Buffer<NSView **>>().Data());
  NSView *contentView = *contentViewPointer;
  [[contentView window] makeKeyAndOrderFront:nil];
}

// Gets a list of processes that are accessing input (microphone)
Napi::Value GetRunningInputAudioProcesses(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  NSError *error = nil;
  NSArray *processes = [AudioProcessMonitor getRunningInputAudioProcesses:&error];
  if (error) {
    Napi::Error::New(env, [error.localizedDescription UTF8String]).ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Array result = Napi::Array::New(env);
  for (NSUInteger i = 0; i < [processes count]; i++) {
      NSString *process = [processes objectAtIndex:i];
      result.Set(i, Napi::String::New(env, [process UTF8String]));
  }

  return result;
}

// Start monitoring microphone usage
Napi::Value StartMonitoringMic(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "Expected a callback function").ThrowAsJavaScriptException();
    return env.Null();
  }

  @try {
    ts_fn = Napi::ThreadSafeFunction::New(
      env,
      info[0].As<Napi::Function>(),
      "MicListener",
      0,
      1,
      []( Napi::Env ) {
        // clean up any resources allocated and passed to the callback 
        // In our case the micState bool is deallocated in the callback 
      }
    );

    // Create and start the monitor
    monitor = [[MicrophoneUsageMonitor alloc] init];
    [monitor startMonitoring:^(BOOL microphoneActive, NSError *error) {
      // Define a struct to hold both the boolean and error
      struct CallbackData {
        bool micActive;
        NSError* error;

        CallbackData(bool active, NSError* err) : micActive(active), error([err retain]) {}
        ~CallbackData() {
          if (error) {
            [error release];
          }
        }
      };

      auto callback = [](Napi::Env env, Napi::Function js_callback, void* data) {

        if (!data) {
          NSLog(@"🎤 Error: data is null");
          return;
        }

        // Cast the void* back to our CallbackData struct
        CallbackData* cbData = static_cast<CallbackData*>(data);

        if (!cbData) {
          NSLog(@"🎤 Error: cbData is null after cast");
          return;
        }

        if (cbData->error != nil) {
          NSString* errorDesc = [cbData->error localizedDescription];
          const char* errorStr = nil;

          if (errorDesc != nil) {
            errorStr = [errorDesc UTF8String];
          }

          Napi::Error err;
          if (errorStr != nil) {
            err = Napi::Error::New(env, errorStr);
          } else {
            err = Napi::Error::New(env, "Unknown error occurred");
          }
          err.Set("code", Napi::Number::New(env, cbData->error.code));
          err.Set("domain", Napi::String::New(env, [cbData->error.domain UTF8String]));

          js_callback.Call({ Napi::Boolean::New(env, cbData->micActive), err.Value() });
        } else {
          js_callback.Call({Napi::Boolean::New(env, cbData->micActive), env.Null()});
        }

        // Clean up
        delete cbData;
      };

      // Create and pass the data to BlockingCall
      CallbackData* data = new CallbackData{static_cast<bool>(microphoneActive), error};

      ts_fn.BlockingCall(data, callback);
    }];

    return Napi::Boolean::New(env, true);
  } @catch (NSException *exception) {
    Napi::Error::New(env, "Exception occurred while starting monitoring").ThrowAsJavaScriptException();
    return env.Null();
  }
}

// Stop monitoring microphone usage
Napi::Value StopMonitoringMic(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (monitor) {
    [monitor stopMonitoring];
    monitor = nil;
  }

  if (ts_fn) {
    ts_fn.Release();
  }

  return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "makeKeyAndOrderFront"),
              Napi::Function::New(env, MakeKeyAndOrderFront));

  exports.Set(Napi::String::New(env, "getRunningInputAudioProcesses"),
              Napi::Function::New(env, GetRunningInputAudioProcesses));

  exports.Set(Napi::String::New(env, "startMonitoringMic"),
              Napi::Function::New(env, StartMonitoringMic));

  exports.Set(Napi::String::New(env, "stopMonitoringMic"),
              Napi::Function::New(env, StopMonitoringMic));

  return exports;
}

NODE_API_MODULE(active_app, Init)
