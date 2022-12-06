#import <Foundation/Foundation.h>

#import "library/objective-c/EnvoyAliases.h"

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyEventTracker : NSObject

@property (nonatomic, copy, nonnull) void (^track)(EnvoyEvent *);

- (instancetype)initWithEventTrackingClosure:(nonnull void (^)(EnvoyEvent *))track;

@end

NS_ASSUME_NONNULL_END
