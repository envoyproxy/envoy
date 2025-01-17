#import <XCTest/XCTest.h>

typedef NSDictionary<NSString *, NSArray<NSString *> *> EnvoyHeaders;
typedef NSDictionary<NSString *, NSString *> EnvoyTags;
typedef NSDictionary<NSString *, NSString *> EnvoyEvent;

#import "library/objective-c/EnvoyBridgeUtility.h"

@interface EnvoyBridgeUtilityTest : XCTestCase
@end

@implementation EnvoyBridgeUtilityTest

- (void)testToNativeData {
  NSString *testString = @"abc";
  NSData *testData = [testString dataUsingEncoding:NSUTF8StringEncoding];
  envoy_data nativeData = toNativeData(testData);
  XCTAssertEqual(memcmp(nativeData.bytes, testData.bytes, 3), 0);
}

- (void)testToManagedNativeStringUsingUTF8Chars {
  NSString *testString = @"台灣大哥大";
  envoy_data stringData = toManagedNativeString(testString);
  NSString *roundtripString = to_ios_string(stringData);
  XCTAssertEqual([testString compare:roundtripString options:0], NSOrderedSame);
}

@end
