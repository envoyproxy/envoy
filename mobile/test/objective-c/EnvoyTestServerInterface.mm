#import "test/objective-c/EnvoyTestServerInterface.h"
#import "test/common/integration/test_server_interface.h"

@implementation TestServerInterface

+ (void)startQuicTestServer {
    start_server(true, false);
}

+ (NSInteger)getServerPort {
    return get_server_port();
}

+ (void)shutdownQuicTestServer {
    shutdown_server();
}

+ (void)startTestServer {
    printf("START SERVER --------------------------");
    start_server(false, false);
}

+ (void)startHttpTestServer {
    printf("START SERVER --------------------------");
    start_server(false, true);
}

+ (void)shutdownTestServer {
    printf("SHUTDOWN SERVER --------------------------");
    shutdown_server();
}

@end
