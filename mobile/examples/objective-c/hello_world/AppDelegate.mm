#import "AppDelegate.h"
#import <Envoy/Envoy-Swift.h>
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

  self.envoy = [[EnvoyBuilder new] buildAndReturnError:NULL];
  NSLog(@"Finished launching!");
  return YES;
}

@end
