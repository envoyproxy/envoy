#import <UIKit/UIKit.h>
#import "ViewController.h"

#pragma mark - Constants

NSString *_CELL_ID = @"cell-id";
NSString *_ENDPOINT = @"http://localhost:9001/api.lyft.com/static/demo/hello_world.txt";

#pragma mark - Result

/// Represents a response from the server or an error.
@interface Result : NSObject
@property (nonatomic, assign) int identifier;
@property (nonatomic, strong) NSString *body;
@property (nonatomic, strong) NSString *serverHeader;
@property (nonatomic, strong) NSString *error;
@end

@implementation Result
@end

#pragma mark - ViewController

@interface ViewController ()
@property (nonatomic, strong) NSURLSession *session;
@property (nonatomic, assign) int requestCount;
@property (nonatomic, strong) NSMutableArray<Result *> *results;
@property (nonatomic, weak) NSTimer *requestTimer;
@end

@implementation ViewController

#pragma mark - Lifecycle

- (instancetype)init {
  self = [super init];
  if (self) {
    NSURLSessionConfiguration *configuration =
        [NSURLSessionConfiguration defaultSessionConfiguration];
    configuration.URLCache = nil;
    configuration.timeoutIntervalForRequest = 10;
    self.session = [NSURLSession sessionWithConfiguration:configuration];

    self.results = [NSMutableArray new];
    self.tableView.allowsSelection = NO;
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
  // Note that the request is sent to the envoy thread listening locally on port 9001.
  NSURL *url = [NSURL URLWithString:_ENDPOINT];
  NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:url];
  [request addValue:@"s3.amazonaws.com" forHTTPHeaderField:@"host"];
  NSLog(@"Starting request to '%@'", url.path);

  self.requestCount++;
  int requestID = self.requestCount;

  __weak ViewController *weakSelf = self;
  NSURLSessionDataTask *task =
      [self.session dataTaskWithRequest:request
                      completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                          [weakSelf handleResponse:(NSHTTPURLResponse *)response
                                              data:data
                                        identifier:requestID
                                             error:error];
                        });
                      }];
  [task resume];
}

- (void)handleResponse:(NSHTTPURLResponse *)response
                  data:(NSData *)data
            identifier:(int)identifier
                 error:(NSError *)error {
  if (error != nil) {
    NSLog(@"Received error: %@", error);
    [self addResponseBody:nil serverHeader:nil identifier:identifier error:error.description];
    return;
  }

  if (response.statusCode != 200) {
    NSString *message = [NSString stringWithFormat:@"failed with status: %ld", response.statusCode];
    [self addResponseBody:nil serverHeader:nil identifier:identifier error:message];
    return;
  }

  NSString *body = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  if (body == nil) {
    NSString *message = @"failed to deserialize body";
    [self addResponseBody:nil serverHeader:nil identifier:identifier error:message];
    return;
  }

  // Deserialize the response, which will include a `Server` header set by Envoy.
  NSString *serverHeader = [[response allHeaderFields] valueForKey:@"Server"];
  NSLog(@"Response:\n%ld bytes\n%@\n%@", data.length, body, [response allHeaderFields]);
  [self addResponseBody:body serverHeader:serverHeader identifier:identifier error:nil];
}

- (void)addResponseBody:(NSString *)body
           serverHeader:(NSString *)serverHeader
             identifier:(int)identifier
                  error:(NSString *)error {
  Result *result = [Result new];
  result.identifier = identifier;
  result.body = body;
  result.serverHeader = serverHeader;
  result.error = error;

  [self.results insertObject:result atIndex:0];
  [self.tableView reloadData];
}

#pragma mark - UITableView

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
  return 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
  return self.results.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView
         cellForRowAtIndexPath:(NSIndexPath *)indexPath {
  UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:_CELL_ID];
  if (cell == nil) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle
                                  reuseIdentifier:_CELL_ID];
  }

  Result *result = self.results[indexPath.row];
  if (result.error == nil) {
    cell.textLabel.text = [NSString stringWithFormat:@"[%d] %@", result.identifier, result.body];
    cell.detailTextLabel.text =
        [NSString stringWithFormat:@"'Server' header: %@", result.serverHeader];

    cell.textLabel.textColor = [UIColor blackColor];
    cell.detailTextLabel.textColor = [UIColor blackColor];
    cell.contentView.backgroundColor = [UIColor whiteColor];
  } else {
    cell.textLabel.text = [NSString stringWithFormat:@"[%d]", result.identifier];
    cell.detailTextLabel.text = result.error;

    cell.textLabel.textColor = [UIColor whiteColor];
    cell.detailTextLabel.textColor = [UIColor whiteColor];
    cell.contentView.backgroundColor = [UIColor redColor];
  }

  return cell;
}

@end
