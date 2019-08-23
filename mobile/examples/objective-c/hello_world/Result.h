#import <Foundation/Foundation.h>

/// Represents a response from the server or an error.
@interface Result : NSObject
@property (nonatomic, assign) int identifier;
@property (nonatomic, strong) NSString *body;
@property (nonatomic, strong) NSString *serverHeader;
@property (nonatomic, strong) NSString *error;
@end
