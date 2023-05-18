#import "test/objective-c/EnvoyTestServer.h"
#import "test/common/integration/test_server_interface.h"


@implementation EnvoyTestServer

// Starts a server with HTTP3 and TLS.
+ (void)startQuicTestServer {
  start_server(true, false);
}

// Get the port of the upstream server.
+ (NSInteger)getEnvoyPort {
  return get_server_port();
}

// Starts a server with HTTP2 and TLS.
+ (void)startTestServer {
  start_server(false, false);
}

// Starts a server with HTTP1 and no TLS.
+ (void)startHttpTestServer {
  start_server(false, true);
}

// Shut down and clean up server.
+ (void)shutdownTestServer {
  shutdown_server();
}

// Add response data to the upstream.
+ (void)setHeadersAndData:(NSString *)header_key
              header_value:(NSString *)header_value
                     data:(NSString *)data {
  set_headers_and_data([header_key UTF8String], [header_value UTF8String], [data UTF8String]);
}

@end
