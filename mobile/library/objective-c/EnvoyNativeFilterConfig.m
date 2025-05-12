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

@end
