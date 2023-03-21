#import <Foundation/Foundation.h>

@interface EnvoyProxySettings : NSObject

@property (nonatomic, strong, readonly) NSString *host;
@property (nonatomic, assign, readonly) uint16_t port;
@property (nonatomic, assign, readonly) BOOL isDirect;

- (instancetype)initWithHost:(NSString *)host port:(uint16_t)port;

+ (instancetype)directProxy;

@end
