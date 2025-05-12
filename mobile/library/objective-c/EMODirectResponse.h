#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, EMOMatchMode) {
  EMOMatchModeContains,
  EMOMatchModeExact,
  EMOMatchModePrefix,
  EMOMatchModeSuffix
};

@interface EMOHeaderMatcher : NSObject

@property (nonatomic, strong) NSString *name;
@property (nonatomic, strong) NSString *value;
@property (nonatomic) EMOMatchMode mode;

@end

@interface EMORouteMatcher : NSObject

@property (nonatomic, strong, nullable) NSString *fullPath;
@property (nonatomic, strong, nullable) NSString *pathPrefix;
@property (nonatomic, strong) NSArray<EMOHeaderMatcher *> *headers;

@end

@interface EMODirectResponse : NSObject

@property (nonatomic, strong) EMORouteMatcher *matcher;
@property (nonatomic) NSUInteger status;
@property (nonatomic, strong, nullable) NSString *body;
@property (nonatomic, strong) NSDictionary<NSString *, NSString *> *headers;

@end

NS_ASSUME_NONNULL_END
