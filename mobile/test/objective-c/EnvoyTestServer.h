#import <Foundation/Foundation.h>

// Interface for starting and managing a test server. Calls into to test_server.cc
@interface EnvoyTestServer : NSObject

// Starts a server with HTTP3 and TLS.
+ (void)startHttp3Server;
// Get the port of the upstream server.
+ (NSInteger)getEnvoyPort;
// Starts a server with HTTP2 and TLS.
+ (void)startHttp2Server;
// Starts a server with HTTP1 and no TLS.
+ (void)startHttp1PlaintextServer;
// Shut down and clean up server.
+ (void)shutdownTestServer;
// Add response data to the upstream.
+ (void)setHeadersAndData:(NSString *)header_key
             header_value:(NSString *)header_value
            response_body:(NSString *)response_body;

@end
