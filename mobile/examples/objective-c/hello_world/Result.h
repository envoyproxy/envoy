#import <Foundation/Foundation.h>

/// Represents a response from the server or an error.
@interface Result : NSObject
@property (nonatomic, strong) NSString *message;
@property (nonatomic, strong) NSString *headerMessage;
@property (nonatomic, strong) NSString *error;
@end
