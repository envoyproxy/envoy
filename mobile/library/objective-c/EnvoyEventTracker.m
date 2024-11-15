#import "library/objective-c/EnvoyEngine.h"

@implementation EnvoyEventTracker

- (instancetype)initWithEventTrackingClosure:(void (^)(EnvoyEvent *))track {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.track = track;
  return self;
}

@end
