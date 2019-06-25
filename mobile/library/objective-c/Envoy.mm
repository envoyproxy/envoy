#import "library/objective-c/Envoy.h"

#import "library/common/main_interface.h"

static NSString *const kConfig = @"config";
static NSString *const kLogLevel = @"logLevel";

@interface Envoy ()
@property (nonatomic, strong) NSThread *runner;
@end

@implementation Envoy

@synthesize runner;

- (instancetype)initWithConfig:(NSString *)config {
  self = [self initWithConfig:config logLevel:@"info"];
  return self;
}

- (instancetype)initWithConfig:(NSString *)config logLevel:(NSString *) logLevel {
  self = [super init];
  if (self) {
    NSDictionary *args = @{
      kConfig: config,
      kLogLevel: logLevel,
    };
    self.runner = [[NSThread alloc] initWithTarget:self selector:@selector(run:) object:args];
    [self.runner start];
  }
  return self;
}

- (BOOL)isRunning {
  return self.runner.isExecuting;
}

- (BOOL)isTerminated {
  return self.runner.isFinished;
}

#pragma mark private

- (void)run:(NSDictionary *)args {
  try {
    run_envoy([args[kConfig] UTF8String], [args[kLogLevel] UTF8String]);
  } catch (NSException *e) {
    NSLog(@"Envoy exception: %@", e);
    NSDictionary *userInfo = @{ @"exception": e};
    [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyException"
                                        object:self
                                        userInfo:userInfo];
  }
}

@end
