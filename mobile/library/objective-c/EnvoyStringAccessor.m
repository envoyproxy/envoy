#import "library/objective-c/EnvoyEngine.h"

@implementation EnvoyStringAccessor

- (instancetype)initWithBlock:(NSString * (^)())block {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.getEnvoyString = block;
  return self;
}

@end
