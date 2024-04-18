#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyNativeFilterConfig : NSObject

@property (nonatomic, strong) NSString *name;
@property (nonatomic, strong) NSString *typedConfig;

- (instancetype)initWithName:(NSString *)name typedConfig:(NSString *)typedConfig;

@end

NS_ASSUME_NONNULL_END
