#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Logging interface.
@interface EnvoyLogger : NSObject

@property (nonatomic, copy) void (^log)(NSInteger, NSString *);

/**
 Create a new instance of the logger.
 */
- (instancetype)initWithLogClosure:(void (^)(NSInteger, NSString *))log;

@end

NS_ASSUME_NONNULL_END
