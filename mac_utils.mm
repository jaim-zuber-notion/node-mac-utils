#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import "AudioProcessMonitor.h"
#include <napi.h>

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
Napi::Value GetActiveAudioProcesses(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  NSError *error = nil;
  NSArray *processes = [AudioProcessMonitor getActiveAudioProcesses:&error];
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

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "makeKeyAndOrderFront"),
              Napi::Function::New(env, MakeKeyAndOrderFront));

  exports.Set(Napi::String::New(env, "getActiveAudioProcesses"),
              Napi::Function::New(env, GetActiveAudioProcesses));

  return exports;
}


NODE_API_MODULE(active_app, Init)
