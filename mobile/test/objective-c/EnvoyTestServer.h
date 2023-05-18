#import <Foundation/Foundation.h>

// Interface for starting and managing a test server. Calls into to test_server.cc
@interface EnvoyTestServer : NSObject

+ (void)startQuicTestServer;
+ (NSInteger)getEnvoyPort;
+ (void)startTestServer;
+ (void)startHttpTestServer;
+ (void)shutdownTestServer;
+ (void)setHeadersAndData:(NSString *)header_key
             header_value:(NSString *)header_value
                     data:(NSString *)data;

@end
