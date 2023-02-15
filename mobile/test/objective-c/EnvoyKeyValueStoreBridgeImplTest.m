#import <XCTest/XCTest.h>

#import "library/objective-c/EnvoyKeyValueStoreBridgeImpl.h"
#import "library/objective-c/EnvoyBridgeUtility.h"

@interface TestKeyValueStore : NSObject <EnvoyKeyValueStore>

@property (nonatomic, copy) NSString * (^readValueBlock)(NSString *key);
@property (nonatomic, copy) void (^saveValueBlock)(NSString *key, NSString *value);
@property (nonatomic, copy) void (^removeValueBlock)(NSString *key);

@end

@implementation TestKeyValueStore

- (NSString *_Nullable)readValueForKey:(NSString *)key {
  return self.readValueBlock(key);
}

- (void)saveValue:(NSString *)value toKey:(NSString *)key {
  self.saveValueBlock(key, value);
}

- (void)removeKey:(NSString *)key {
  self.removeValueBlock(key);
}

@end

@interface EnvoyEngineImplTest : XCTestCase
@end

@implementation EnvoyEngineImplTest

- (void)testReadValue {
  NSString *expectedKey = @"key";
  NSString *expectedValue = @"value";

  TestKeyValueStore *store = [TestKeyValueStore new];
  __block NSString *actualKey = @"";
  store.readValueBlock = ^NSString *(NSString *key) {
    actualKey = key;
    return expectedValue;
  };

  NSData *keyData = [expectedKey dataUsingEncoding:NSUTF8StringEncoding];
  envoy_data data = ios_kv_store_read(toNativeData(keyData), CFBridgingRetain(store));
  NSData *valueData = to_ios_data(data);
  NSString *actualValue = [[NSString alloc] initWithBytes:valueData.bytes
                                                   length:valueData.length
                                                 encoding:NSUTF8StringEncoding];

  XCTAssertEqualObjects(expectedKey, actualKey);
  XCTAssertEqualObjects(expectedValue, actualValue);
}

- (void)testSaveValue {
  NSString *expectedKey = @"key";
  NSString *expectedValue = @"value";

  TestKeyValueStore *store = [TestKeyValueStore new];
  __block NSString *actualKey = @"";
  __block NSString *actualValue = @"";
  store.saveValueBlock = ^void(NSString *key, NSString *value) {
    actualKey = key;
    actualValue = value;
  };

  NSData *keyData = [expectedKey dataUsingEncoding:NSUTF8StringEncoding];
  NSData *valueData = [expectedValue dataUsingEncoding:NSUTF8StringEncoding];
  ios_kv_store_save(toNativeData(keyData), toNativeData(valueData), CFBridgingRetain(store));

  XCTAssertEqualObjects(expectedKey, actualKey);
  XCTAssertEqualObjects(expectedValue, actualValue);
}

- (void)testRemoveValue {
  NSString *expectedKey = @"key";

  TestKeyValueStore *store = [TestKeyValueStore new];
  __block NSString *actualKey = @"";
  store.removeValueBlock = ^void(NSString *key) {
    actualKey = key;
  };

  NSData *keyData = [expectedKey dataUsingEncoding:NSUTF8StringEncoding];
  ios_kv_store_remove(toNativeData(keyData), CFBridgingRetain(store));

  XCTAssertEqualObjects(expectedKey, actualKey);
}

@end
