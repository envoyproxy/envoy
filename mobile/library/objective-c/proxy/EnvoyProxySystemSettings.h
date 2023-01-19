#import <Foundation/Foundation.h>

@interface EnvoyProxySystemSettings : NSObject

@property (nonatomic, strong, readonly) NSURL *pacFileURL;
@property (nonatomic, strong, readonly) NSString *host;
@property (nonatomic, assign, readonly) NSUInteger port;

- (instancetype)initWithHost:(NSString *)host port:(NSUInteger)port;
- (instancetype)initWithPACFileURL:(NSURL *)pacFileURL;

@end
