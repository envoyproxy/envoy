#import "test/objective-c/EnvoyTestServer.h"
#import "test/common/integration/test_server_interface.h"

@implementation EnvoyTestServer

+ (NSInteger)getEnvoyPort {
  return get_server_port();
}

+ (void)startHttp1PlaintextServer {
  start_server(Envoy::TestServerType::HTTP1_WITHOUT_TLS);
}

+ (void)startHttpProxyServer {
  start_server(Envoy::TestServerType::HTTP_PROXY);
}

+ (void)startHttpsProxyServer {
  start_server(Envoy::TestServerType::HTTPS_PROXY);
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
