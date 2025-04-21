#import "MicrophoneUsageMonitor.h"

static NSString * const errorDomain = @"com.MicrophoneUsageMonitor";

@interface MicrophoneUsageMonitor ()

@property (nonatomic, assign) AudioDeviceID micDeviceID;
@property (nonatomic, copy) void (^callback)(UInt32 inNumberAddresses, const AudioObjectPropertyAddress *inAddresses);
@property (nonatomic, copy) void (^storedCompletion)(BOOL microphoneActive, NSError * _Nullable error);

- (AudioDeviceID)getDefaultInputDeviceIDWithError:(NSError **) error;

@end

static AudioObjectPropertyAddress deviceIsAliveAddress = (AudioObjectPropertyAddress){
      .mSelector = kAudioDevicePropertyDeviceIsAlive,
      .mScope = kAudioObjectPropertyScopeGlobal,
      .mElement = kAudioObjectPropertyElementMain
};

static AudioObjectPropertyAddress micPropertyAddress = (AudioObjectPropertyAddress){
    .mSelector = kAudioDevicePropertyDeviceIsRunningSomewhere,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain
  };

static AudioObjectPropertyAddress defaultInputDeviceAddress = (AudioObjectPropertyAddress){
    .mSelector = kAudioHardwarePropertyDefaultInputDevice,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain
  };

@implementation MicrophoneUsageMonitor {}

- (void)restartMonitoring {
  [self cleanup];

  self.storedCompletion(NO, [self makeErrorWithCode:0 message: @"Waiting to restart monitoring"]);
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)),
                 dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
       self.storedCompletion(NO, [self makeErrorWithCode:0 message: @"✅ Restarting monitoring"]);
       [self startMonitoringInternal:self.storedCompletion];
   });
  }

-(void)startMonitoringInternal:(void (^)(BOOL microphoneActive, NSError * _Nullable error))completion {

  NSError *error = nil;
  self.micDeviceID = [self getDefaultInputDeviceIDWithError:&error];

  if (error) {
    completion(NO, error);
    return;
  }

  self.callback = [self createCallback:completion micDeviceID:self.micDeviceID micPropertyAddress:micPropertyAddress];

  OSStatus addStatus = AudioObjectAddPropertyListenerBlock(self.micDeviceID,
                                    &micPropertyAddress,
                                    dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                                    self.callback);

  if (addStatus != noErr) {
    completion(NO, [self makeErrorWithCode:addStatus message:@"Error in AudioObjectAddPropertyListenerBlock"]);
    return;
  }
}

- (void)startMonitoring:(void (^)(BOOL microphoneActive, NSError * _Nullable error))completion {
  self.storedCompletion = completion;

  NSError *error = nil;
  self.micDeviceID = [self getDefaultInputDeviceIDWithError:&error];

  if (error) {
    completion(NO, error);
    return;
  }

  OSStatus microphoneAddressStatus = AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject,
                                                         &defaultInputDeviceAddress,
                                                         dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                                                         [self createMicrophoneCallback]);

  if (microphoneAddressStatus != noErr) {
    completion(NO, [self makeErrorWithCode:microphoneAddressStatus message:@"Error in AudioObjectAddPropertyListenerBlock"]);
    return;
  }

  // Trigger initial check
  UInt32 microphoneInUse = 0;
  UInt32 size = sizeof(UInt32);
  OSStatus status = AudioObjectGetPropertyData(self.micDeviceID,
                                             &micPropertyAddress,
                                             0,
                                             NULL,
                                             &size,
                                             &microphoneInUse);

  if (status == noErr) {
    completion((BOOL)microphoneInUse, nil);
  }

  [self startMonitoringInternal:completion];
}

- (AudioDeviceID)getDefaultInputDeviceIDWithError:(NSError **) error {
    AudioDeviceID deviceID = kAudioObjectUnknown;
    UInt32 size = sizeof(AudioDeviceID);

    AudioObjectPropertyAddress address = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                               &address,
                                               0,
                                               NULL,
                                               &size,
                                               &deviceID);

    if (status != noErr) {
      *error = [NSError errorWithDomain:errorDomain
                                            code:status
                                        userInfo:@{NSLocalizedDescriptionKey: @"Failed to get default input device ID from AudioObjectGetPropertyData"}];
    }

  return deviceID;
}

- (void (^)(UInt32 inNumberAddresses, const AudioObjectPropertyAddress *inAddresses))createMicrophoneCallback {
  __weak typeof(self) weakSelf = self;

  return ^(UInt32 inNumberAddresses, const AudioObjectPropertyAddress *inAddresses) {

    __strong typeof(weakSelf) strongSelf = weakSelf;
    if (!strongSelf) return;

    [strongSelf restartMonitoring];
  };
}

- (void (^)(UInt32 inNumberAddresses, const AudioObjectPropertyAddress *inAddresses))createCallback:(void (^)(BOOL microphoneActive, NSError * _Nullable error))completion 
              micDeviceID:(AudioDeviceID)micDeviceID
              micPropertyAddress:(AudioObjectPropertyAddress)micPropertyAddress {

  return ^(UInt32 inNumberAddresses, const AudioObjectPropertyAddress *inAddresses) {

    UInt32 isAlive = 0;
    UInt32 size = sizeof(UInt32);
    OSStatus aliveStatus = AudioObjectGetPropertyData(micDeviceID,
                                               &deviceIsAliveAddress,
                                               0,
                                               NULL,
                                               &size,
                                               &isAlive);

    if (aliveStatus != noErr) {
      // microphone is not active, running status will not be available
      // This can occur when the microphone is unplugged. But before the default device is updated.
      return;
    }

    if (isAlive == 0) {
      NSError *error = [self makeErrorWithCode:aliveStatus message:@"Device is not alive - running status will not be valid"];
      completion(NO, error);
      return;
    }

    UInt32 isRunning = 0;
    OSStatus runningStatus = AudioObjectGetPropertyData(micDeviceID,
                                       &micPropertyAddress,
                                       0,
                                       NULL,
                                       &size,
                                       &isRunning);

    if (runningStatus == noErr) {
      completion((BOOL)isRunning, nil);
    } else {
      completion(NO, [self makeErrorWithCode:runningStatus message:@"Error in AudioObjectGetPropertyData - may be transient"]);
    }
  };
}

- (void)cleanup {
  if (self.callback) {
    NSString *message = [NSString stringWithFormat:@"Removing AudioObjectRemovePropertyListenerBlock for micDeviceID: %u", self.micDeviceID];
    self.storedCompletion(NO, [self makeErrorWithCode:0 message: message]);
    AudioObjectRemovePropertyListenerBlock(self.micDeviceID,
                                         &micPropertyAddress,
                                         dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                                         self.callback);
    self.callback = nil;
  }
}

- (void)stopMonitoring {
  [self cleanup];
}

- (void)dealloc {
  [self stopMonitoring];
}

- (NSError*)makeErrorWithCode:(OSStatus)code message:(NSString*)message {
  return [NSError errorWithDomain:errorDomain
                             code:code
                         userInfo:@{NSLocalizedDescriptionKey: message}];
}

@end
