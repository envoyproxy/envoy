#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyNativeFilterConfig : NSObject

@property (nonatomic, strong) NSString *name;
// Text-format (full protos) or binary-serialized google.protobuf.Any (lite protos).
// Exactly one of typedConfig or typedConfigData must be set.
@property (nonatomic, strong) NSString *typedConfig;
// Binary-serialized google.protobuf.Any bytes. Takes precedence over typedConfig when set.
@property (nonatomic, strong, nullable) NSData *typedConfigData;

- (instancetype)initWithName:(NSString *)name typedConfig:(NSString *)typedConfig;
- (instancetype)initWithName:(NSString *)name typedConfigData:(NSData *)typedConfigData;

@end

NS_ASSUME_NONNULL_END
