#import "test/objective-c/EnvoyTestServer.h"
#import "test/common/integration/test_server_interface.h"

@implementation EnvoyTestServer

+ (void)startHttp3Server {
  start_server(Envoy::TestServerType::HTTP3_HTTPS);
}

+ (NSInteger)getEnvoyPort {
  return get_server_port();
}

+ (void)startHttp2Server {
  start_server(Envoy::TestServerType::HTTP2_HTTPS);
}

+ (void)startHttp1PlaintextServer {
  start_server(Envoy::TestServerType::HTTP1_HTTP);
}

+ (void)shutdownTestServer {
  shutdown_server();
}

+ (void)setHeadersAndData:(NSString *)header_key
             header_value:(NSString *)header_value
            response_body:(NSString *)response_body {
  set_headers_and_data([header_key UTF8String], [header_value UTF8String],
                       [response_body UTF8String]);
}

@end
