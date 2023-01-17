#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySettings.h"

@implementation EnvoyProxySettings

- (instancetype)initWithHost:(NSString *)host port:(NSUInteger)port {
  self = [super init];
  if (self) {
    self.host = host;
    self.port = port;
  }

  return self;
}

- (instancetype)initWithPACFileURL:(id)pacFileURL {
  self = [super init];
  if (self) {
    self.pacFileURL = pacFileURL;
  }

  return self;
}

- (BOOL)isEqual:(id)object {
  if (![object isKindOfClass:[EnvoyProxySettings class]]) {
    return false;
  }

  EnvoyProxySettings *rhs = (EnvoyProxySettings *)object;
  return [self.host isEqual:rhs.host]
    && self.port == rhs.port;
}

@end
