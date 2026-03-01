#import <XCTest/XCTest.h>

#import "library/objective-c/EnvoyEngine.h"

@interface EnvoyEngineImpl (Testing)
- (BOOL)isNetworkMonitorEnabledForTesting;
@end

@interface EnvoyEngineImplNetworkMonitoringTest : XCTestCase
@end

@implementation EnvoyEngineImplNetworkMonitoringTest

- (void)assertNetworkMonitoringMode:(int)mode monitorEnabled:(BOOL)expectedEnabled {
  EnvoyEngineImpl *engine = [[EnvoyEngineImpl alloc] initWithRunningCallback:nil
                                                                      logger:nil
                                                                eventTracker:nil
                                                       networkMonitoringMode:mode];

  XCTAssertEqual([engine isNetworkMonitorEnabledForTesting], expectedEnabled);
}

- (void)testNetworkMonitoringModeDisabledDoesNotCreateMonitor {
  [self assertNetworkMonitoringMode:0 monitorEnabled:NO];
}

- (void)testNetworkMonitoringModeReachabilityCreatesMonitor {
  [self assertNetworkMonitoringMode:1 monitorEnabled:YES];
}

- (void)testNetworkMonitoringModePathMonitorCreatesMonitor {
  [self assertNetworkMonitoringMode:2 monitorEnabled:YES];
}

@end
