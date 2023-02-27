#import <XCTest/XCTest.h>

#import "library/objective-c/EnvoyConfiguration.mm"

@interface EnvoyConfigurationTest : XCTestCase
@end

@implementation EnvoyConfigurationTest

- (void)testGenerateYamlWithGrpcStatsDomain {
  NSString *grpcStatsDomain = @"example.grpcstatsdomain.com";
  EnvoyConfiguration *config =
      [[EnvoyConfiguration alloc] initWithAdminInterfaceEnabled:YES
                                                grpcStatsDomain:grpcStatsDomain];
  NSString *yamlString = [config generateYaml];
  XCTAssertTrue([yamlString containsString:grpcStatsDomain]);
}

- (void)testGenerateDefaultYamlNotIncludeRtdsAds {
  EnvoyConfiguration *config = [[EnvoyConfiguration alloc] initWithAdminInterfaceEnabled:NO];
  NSString *yamlString = [config generateYaml];
  XCTAssertTrue([yamlString notContainsString:"rtds_layer:"]);
  XCTAssertTrue([yamlString notContainsString:"ads_config:"]);
}

@end