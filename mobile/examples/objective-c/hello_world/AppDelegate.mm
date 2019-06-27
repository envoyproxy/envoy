#import "AppDelegate.h"
#import <Envoy/Envoy.h>
#import <UIKit/UIKit.h>
#import "ViewController.h"

@interface AppDelegate ()
@property (nonatomic, strong) Envoy *envoy;
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  UIViewController *controller = [ViewController new];
  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
  [self.window setRootViewController:controller];
  [self.window makeKeyAndVisible];

  NSString *configFile = [[NSBundle mainBundle] pathForResource:@"config" ofType:@"yaml"];
  NSString *configYaml = [NSString stringWithContentsOfFile:configFile
                                                   encoding:NSUTF8StringEncoding
                                                      error:NULL];
  NSLog(@"Loading config:\n%@", configYaml);
  self.envoy = [[Envoy alloc] initWithConfig:configYaml];
  NSLog(@"Finished launching!");
  return YES;
}

@end
