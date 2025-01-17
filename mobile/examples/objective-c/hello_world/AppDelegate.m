#import "AppDelegate.h"
#import <UIKit/UIKit.h>
#import "ViewController.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  UIViewController *controller = [ViewController new];
  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
  [self.window setRootViewController:controller];
  [self.window makeKeyAndVisible];

  NSLog(@"Finished launching!");
  return YES;
}

@end
