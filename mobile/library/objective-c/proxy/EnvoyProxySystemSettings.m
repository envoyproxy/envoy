#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySystemSettings.h"

@interface EnvoyProxySystemSettings ()

@property (nonatomic, strong) NSURL *pacFileURL;
@property (nonatomic, strong) NSString *host;
@property (nonatomic, assign) NSUInteger port;

@end

@implementation EnvoyProxySystemSettings

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
  if (![object isKindOfClass:[EnvoyProxySystemSettings class]]) {
    return false;
  }

  EnvoyProxySystemSettings *rhs = (EnvoyProxySystemSettings *)object;
  return [self.host isEqual:rhs.host]
    && self.port == rhs.port;
}

@end
