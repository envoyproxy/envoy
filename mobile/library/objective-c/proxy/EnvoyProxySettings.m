#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySettings.h"

@interface EnvoyProxySettings ()

@property (nonatomic, strong) NSString *host;
@property (nonatomic, assign) NSUInteger port;

@end

@implementation EnvoyProxySettings

- (instancetype)initWithHost:(NSString *)host port:(NSUInteger)port {
  self = [super init];
  if (self) {
    self.host = host;
    self.port = port;
  }

  return self;
}

+ (instancetype)directProxy {
  return [[EnvoyProxySettings alloc] initWithHost:@"" port:0];
}

- (BOOL)isDirect {
  return [self.host isEqualToString:@""] && self.port == 0;
}

@end
