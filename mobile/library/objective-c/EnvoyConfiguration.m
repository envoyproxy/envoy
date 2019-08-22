#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

@implementation EnvoyConfiguration

+ (NSString *)templateString {
  return [[NSString alloc] initWithUTF8String:config_template];
}

@end
