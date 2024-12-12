#pragma once

#import <Foundation/Foundation.h>

// Interface for starting and managing a test server. Calls into to test_server.cc
//
// NB: Any test that utilizes this class must have a `"sandboxNetwork": "standard"`
// `exec_properties` in its BUILD target to allow binding a listening socket on
// the EngFlow machines
// (https://docs.engflow.com/re/client/platform-options-reference.html#sandboxallowed).
@interface EnvoyTestServer : NSObject

// Get the port of the upstream HTTP server.
+ (NSInteger)getHttpPort;
// Get the port of the upstream proxy server.
+ (NSInteger)getProxyPort;
// Starts a server with HTTP 1.
+ (void)startHttp1Server;
// Starts a server with HTTPS 1.
+ (void)startHttps1Server;
// Starts a server with HTTPS 2.
+ (void)startHttps2Server;
// Starts a server as a HTTP proxy.
+ (void)startHttpProxyServer;
// Starts a server as a HTTPS proxy.
+ (void)startHttpsProxyServer;
// Shut down and clean up the HTTP server.
+ (void)shutdownTestHttpServer;
// Shut down and clean up the Proxy server.
+ (void)shutdownTestProxyServer;
// Add response data to the HTTP server.
+ (void)setHeadersAndData:(NSString *)header_key
             header_value:(NSString *)header_value
            response_body:(NSString *)response_body;

@end
