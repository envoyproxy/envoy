#pragma once

#import <Foundation/Foundation.h>

// Interface for dealing with custom API registration for test classes.
@interface EnvoyTestApi : NSObject

// Registers a test Proxy Resolver API.
+ (void)registerTestProxyResolver:(NSString *)host
                             port:(NSInteger)port
                   usePacResolver:(BOOL)usePacResolver;

@end
