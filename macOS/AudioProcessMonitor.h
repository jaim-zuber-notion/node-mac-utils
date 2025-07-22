#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

struct AudioProcessResult {
    NSArray<NSString*>* _Nullable processes;
    NSError* _Nullable error;
    bool success;
};

NS_ASSUME_NONNULL_BEGIN

@interface AudioProcessMonitor : NSObject

@property (nonatomic, strong, readonly) NSArray<NSString *> *runningBundleIDs;

- (instancetype)init;
+ (NSArray<NSString *> *)getRunningInputAudioProcesses:(NSError **)error;
+ (struct AudioProcessResult)getProcessesAccessingMicrophoneWithResult;

@end

NS_ASSUME_NONNULL_END
