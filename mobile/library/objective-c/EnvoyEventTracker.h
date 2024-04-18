#pragma once

#import <Foundation/Foundation.h>

#import "library/objective-c/EnvoyAliases.h"

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyEventTracker : NSObject

@property (nonatomic, copy) void (^track)(EnvoyEvent *);

- (instancetype)initWithEventTrackingClosure:(void (^)(EnvoyEvent *))track;

@end

NS_ASSUME_NONNULL_END
