#import <Foundation/Foundation.h>

@interface TestServerInterface : NSObject

+ (void)startQuicTestServer;
+ (NSInteger)getServerPort;
+ (void)shutdownQuicTestServer;
+ (void)startTestServer;
+ (void)startHttpTestServer;
+ (void)shutdownTestServer;

@end
