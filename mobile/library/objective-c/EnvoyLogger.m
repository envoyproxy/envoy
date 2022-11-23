#import "library/objective-c/EnvoyEngine.h"

@implementation EnvoyLogger

- (instancetype)initWithLogClosure:(void (^)(NSString *))log {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.log = log;
  return self;
}
@end
