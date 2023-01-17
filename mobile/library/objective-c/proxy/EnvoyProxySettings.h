#import <Foundation/Foundation.h>

@interface EnvoyProxySettings : NSObject

@property (nonatomic, strong) NSURL *pacFileURL;
@property (nonatomic, strong) NSString *host;
@property (nonatomic, assign) NSUInteger port;

- (instancetype)initWithHost:(NSString *)host port:(NSUInteger)port;
- (instancetype)initWithPACFileURL:(NSURL *)pacFileURL;

@end
