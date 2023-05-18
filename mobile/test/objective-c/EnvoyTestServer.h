#import <Foundation/Foundation.h>

// Interface for starting and managing a test server. Calls into to test_server.cc
@interface EnvoyTestServer : NSObject

+ (void)startQuicTestServer;
+ (NSInteger)getServerPort;
+ (void)startTestServer;
+ (void)startHttpTestServer;
+ (void)shutdownTestServer;
+ (void)setHeadersAndData;

@end
