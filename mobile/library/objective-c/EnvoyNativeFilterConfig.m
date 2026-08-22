#import "library/objective-c/EnvoyEngine.h"

@implementation EnvoyNativeFilterConfig

- (instancetype)initWithName:(NSString *)name typedConfig:(NSString *)typedConfig {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.name = name;
  self.typedConfig = typedConfig;
  return self;
}

- (instancetype)initWithName:(NSString *)name typedConfigData:(NSData *)typedConfigData {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.name = name;
  self.typedConfig = @"";
  self.typedConfigData = typedConfigData;
  return self;
}

@end
