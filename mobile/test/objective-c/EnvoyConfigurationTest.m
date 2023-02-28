#import <XCTest/XCTest.h>

#import "library/objective-c/EnvoyConfiguration.h"
#import "library/objective-c/EnvoyConfiguration.mm"

@interface EnvoyConfigurationTest : XCTestCase
@end

@interface EnvoyConfiguration
- (NSString *)generateYaml;
- (instancetype)initWithAdminInterfaceEnabled;
@end

@implementation EnvoyConfigurationTest

- (void)testGenerateYamlWithGrpcStatsDomain {
  NSString *grpcStatsDomain = @"example.grpcstatsdomain.com";
  EnvoyConfiguration *config = [[EnvoyConfiguration alloc] initWithAdminInterfaceEnabled:YES];
  NSString *yamlString = [config generateYaml];
  XCTAssertTrue([yamlString containsString:grpcStatsDomain]);
}

// - (void)testGenerateDefaultYamlNotIncludeRtdsAds {
//   EnvoyConfiguration *config = [[EnvoyConfiguration alloc] initWithAdminInterfaceEnabled:NO];
//   NSString *yamlString = [config generateYaml];
//   XCTAssertTrue(![yamlString containsString:@"rtds_layer:"]);
//   XCTAssertTrue(![yamlString containsString:@"ads_config:"]);
// }

@end