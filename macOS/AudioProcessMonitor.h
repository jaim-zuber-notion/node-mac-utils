#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

NS_ASSUME_NONNULL_BEGIN

@interface AudioProcessMonitor : NSObject

@property (nonatomic, strong, readonly) NSArray<NSString *> *runningBundleIDs;

- (instancetype)init;
+ (NSArray<NSString *> *)getRunningInputAudioProcesses:(NSError **)error;

@end

NS_ASSUME_NONNULL_END 
