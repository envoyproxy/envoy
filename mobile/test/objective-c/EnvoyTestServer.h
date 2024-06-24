#pragma once

#import <Foundation/Foundation.h>

// Interface for starting and managing a test server. Calls into to test_server.cc
//
// NB: Any test that utilizes this class must have a `"sandboxNetwork": "standard"`
// `exec_properties` in its BUILD target to allow binding a listening socket on
// the EngFlow machines
// (https://docs.engflow.com/re/client/platform-options-reference.html#sandboxallowed).
@interface EnvoyTestServer : NSObject

// Get the port of the upstream server.
+ (NSInteger)getEnvoyPort;
// Starts a server with HTTP1 and no TLS.
+ (void)startHttp1PlaintextServer;
// Starts a server as a HTTP proxy.
+ (void)startHttpProxyServer;
// Starts a server as a HTTPS proxy.
+ (void)startHttpsProxyServer;
// Shut down and clean up server.
+ (void)shutdownTestServer;
// Add response data to the upstream.
+ (void)setHeadersAndData:(NSString *)header_key
             header_value:(NSString *)header_value
            response_body:(NSString *)response_body;

@end
