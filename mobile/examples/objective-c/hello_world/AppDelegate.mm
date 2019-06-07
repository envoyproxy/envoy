#import "AppDelegate.h"
#import <Envoy/Envoy.h>
#import <UIKit/UIKit.h>
#import "ViewController.h"

@interface AppDelegate ()
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    UIViewController *controller = [ViewController new];
    self.window = [[UIWindow alloc] initWithFrame: [UIScreen mainScreen].bounds];
    [self.window setRootViewController:controller];
    [self.window makeKeyAndVisible];

    [NSThread detachNewThreadSelector:@selector(startEnvoy) toTarget:self withObject:nil];
    NSLog(@"Finished launching!");
    return YES;
}

- (void)startEnvoy {
    NSString *configFile = [[NSBundle mainBundle] pathForResource:@"config" ofType:@"yaml"];
    NSString *configYaml = [NSString stringWithContentsOfFile:configFile encoding:NSUTF8StringEncoding error:NULL];
    NSLog(@"Loading config:\n%@", configYaml);

    // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
    // as needed. Whatever code in the initialization path that fails is expected to log an error
    // message so the user can diagnose.
    try {
        run_envoy(configYaml.UTF8String);
    } catch (NSException *e) {
        NSLog(@"Error starting Envoy: %@", e);
        exit(EXIT_FAILURE);
    }
}

@end
