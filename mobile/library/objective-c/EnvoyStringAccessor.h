#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyStringAccessor : NSObject

@property (nonatomic, copy) NSString * (^getEnvoyString)();

- (instancetype)initWithBlock:(NSString * (^)())block;

@end

NS_ASSUME_NONNULL_END
