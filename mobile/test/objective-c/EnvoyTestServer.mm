#import "test/objective-c/EnvoyTestServer.h"
#import "test/common/integration/test_server_interface.h"

@implementation EnvoyTestServer

+ (NSInteger)getHttpPort {
  return get_http_server_port();
}

+ (NSInteger)getProxyPort {
  return get_proxy_server_port();
}

+ (void)startHttp1PlaintextServer {
  start_http_server(Envoy::TestServerType::HTTP1_WITHOUT_TLS);
}

+ (void)startHttpProxyServer {
  start_proxy_server(Envoy::TestServerType::HTTP_PROXY);
}

+ (void)startHttpsProxyServer {
  start_proxy_server(Envoy::TestServerType::HTTPS_PROXY);
}

+ (void)shutdownTestHttpServer {
  shutdown_http_server();
}

+ (void)shutdownTestProxyServer {
  shutdown_proxy_server();
}

+ (void)setHeadersAndData:(NSString *)header_key
             header_value:(NSString *)header_value
            response_body:(NSString *)response_body {
  set_http_headers_and_data([header_key UTF8String], [header_value UTF8String],
                            [response_body UTF8String]);
}

@end
