#pragma once

#import <Foundation/Foundation.h>

@class EnvoyHTTPFilter;

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyHTTPFilterFactory : NSObject

@property (nonatomic, strong) NSString *filterName;

@property (nonatomic, copy) EnvoyHTTPFilter * (^create)();

@end

NS_ASSUME_NONNULL_END
