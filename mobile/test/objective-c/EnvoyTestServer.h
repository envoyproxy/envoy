#pragma once

#import <Foundation/Foundation.h>

// Interface for starting and managing a test server. Calls into to test_server.cc
//
// NB: Any test that utilizes this class must have a `no-remote-exec` tag in its BUILD target.
// EnvoyTestServer binds to a listening socket on the machine it runs on, and on CI, this
// operation is not permitted in remote execution environments.
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
