#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface MicrophoneUsageMonitor : NSObject

- (void)startMonitoring:(void (^)(BOOL microphoneActive, NSError * _Nullable error))completion;
- (void)stopMonitoring;

@end

NS_ASSUME_NONNULL_END
