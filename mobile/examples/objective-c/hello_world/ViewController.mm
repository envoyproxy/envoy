#import <UIKit/UIKit.h>
#import "ViewController.h"

#pragma mark - Constants

NSString* _CELL_ID = @"cell-id";
NSString* _ENDPOINT = @"http://0.0.0.0:9001/api.lyft.com/static/demo/hello_world.txt";

#pragma mark - ResponseValue

/// Represents a response from the server.
@interface ResponseValue : NSObject
@property (nonatomic, strong) NSString* body;
@property (nonatomic, strong) NSString* serverHeader;
@end

@implementation ResponseValue
@end

#pragma mark - ViewController

@interface ViewController ()
@property (nonatomic, strong) NSMutableArray<ResponseValue*>* responses;
@property (nonatomic, weak) NSTimer* requestTimer;
@end

@implementation ViewController

#pragma mark - Lifecycle

- (instancetype)init {
  self = [super init];
  if (self) {
    self.responses = [NSMutableArray new];
    self.tableView.allowsSelection = FALSE;
  }
  return self;
}

- (void)dealloc {
  [self.requestTimer invalidate];
  self.requestTimer = nil;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  [self startRequests];
}

#pragma mark - Requests

- (void)startRequests {
  // Note that the first delay will give Envoy time to start up.
  self.requestTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                       target:self
                                                     selector:@selector(performRequest)
                                                     userInfo:nil
                                                      repeats:YES];
}

- (void)performRequest {
  NSURLSession* session = [NSURLSession sharedSession];
  // Note that the request is sent to the envoy thread listening locally on port 9001.
  NSURL* url = [NSURL URLWithString:_ENDPOINT];
  NSURLRequest* request = [NSURLRequest requestWithURL:url];
  NSLog(@"Starting request to '%@'", url.path);

  __weak ViewController* weakSelf = self;
  NSURLSessionDataTask* task =
      [session dataTaskWithRequest:request
                 completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                   if (error == nil && [(NSHTTPURLResponse*)response statusCode] == 200) {
                     [weakSelf handleResponse:(NSHTTPURLResponse*)response data:data];
                   } else {
                     NSLog(@"Received error: %@", error);
                   }
                 }];
  [task resume];
}

- (void)handleResponse:(NSHTTPURLResponse*)response data:(NSData*)data {
  NSString* body = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  if (body == nil || response == nil) {
    NSLog(@"Failed to deserialize response string");
    return;
  }

  // Deserialize the response, which will include a `Server` header set by Envoy.
  ResponseValue* value = [ResponseValue new];
  value.body = body;
  value.serverHeader = [[response allHeaderFields] valueForKey:@"Server"];

  NSLog(@"Response:\n%ld bytes\n%@\n%@", data.length, body, [response allHeaderFields]);
  dispatch_async(dispatch_get_main_queue(), ^{
    [self.responses addObject:value];
    [self.tableView reloadData];
  });
}

#pragma mark - UITableView

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView {
  return 1;
}

- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
  return self.responses.count;
}

- (UITableViewCell*)tableView:(UITableView*)tableView
        cellForRowAtIndexPath:(NSIndexPath*)indexPath {
  UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:_CELL_ID];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:_CELL_ID];
  }

  ResponseValue* response = self.responses[indexPath.row];
  cell.textLabel.text = [NSString stringWithFormat:@"Response: %@", response.body];
  cell.detailTextLabel.text =
      [NSString stringWithFormat:@"'Server' header: %@", response.serverHeader];
  return cell;
}

@end
