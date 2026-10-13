#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@protocol EnvoyKeyValueStore

/// Read a value from the key value store implementation.
- (NSString *_Nullable)readValueForKey:(NSString *)key;

/// Save a value to the key value store implementation.
- (void)saveValue:(NSString *)value toKey:(NSString *)key;

/// Remove a value from the key value store implementation.
- (void)removeKey:(NSString *)key;

@end

NS_ASSUME_NONNULL_END
