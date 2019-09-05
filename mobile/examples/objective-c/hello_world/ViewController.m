#import <Envoy/Envoy-Swift.h>
#import <UIKit/UIKit.h>
#import "Result.h"
#import "ViewController.h"

#pragma mark - Constants

NSString *_CELL_ID = @"cell-id";
NSString *_REQUEST_AUTHORITY = @"s3.amazonaws.com";
NSString *_REQUEST_PATH = @"/api.lyft.com/static/demo/hello_world.txt";
NSString *_REQUEST_SCHEME = @"http";

#pragma mark - ViewController

@interface ViewController ()
@property (nonatomic, strong) Envoy *envoy;
@property (nonatomic, assign) int requestCount;
@property (nonatomic, strong) NSMutableArray<Result *> *results;
@property (nonatomic, weak) NSTimer *requestTimer;
@end

@implementation ViewController

#pragma mark - Lifecycle

- (instancetype)init {
  self = [super init];
  if (self) {
    self.results = [NSMutableArray new];
    self.tableView.allowsSelection = NO;
    [self startEnvoy];
  }
  return self;
}

- (void)startEnvoy {
  NSLog(@"Starting Envoy...");
  NSError *error;
  self.envoy = [[EnvoyBuilder new] buildAndReturnError:&error];
  if (error) {
    NSLog(@"Starting Envoy failed: %@", error);
  } else {
    NSLog(@"Started Envoy, beginning requests...");
    [self startRequests];
  }
}

- (void)dealloc {
  [self.requestTimer invalidate];
  self.requestTimer = nil;
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
  self.requestCount++;
  NSLog(@"Starting request to '%@'", _REQUEST_PATH);

  int requestID = self.requestCount;
  RequestBuilder *builder = [[RequestBuilder alloc] initWithMethod:RequestMethodGet
                                                            scheme:_REQUEST_SCHEME
                                                         authority:_REQUEST_AUTHORITY
                                                              path:_REQUEST_PATH];
  Request *request = [builder build];
  ResponseHandler *handler = [[ResponseHandler alloc] initWithQueue:dispatch_get_main_queue()];

  __weak ViewController *weakSelf = self;
  [handler onHeaders:^(NSDictionary<NSString *, NSArray<NSString *> *> *headers,
                       NSInteger statusCode, BOOL endStream) {
    NSLog(@"Response status (%i): %ld\n%@", requestID, statusCode, headers);
    NSString *body = [NSString stringWithFormat:@"Status: %ld", statusCode];
    [weakSelf addResponseBody:body
                 serverHeader:[headers[@"server"] firstObject]
                   identifier:requestID
                        error:nil];
  }];

  [handler onData:^(NSData *data, BOOL endStream) {
    NSLog(@"Response data (%i): %ld bytes", requestID, data.length);
  }];

  [handler onError:^(EnvoyError *error) {
    NSLog(@"Error (%i): Request failed: %@", requestID, error.message);
  }];

  [self.envoy send:request body:nil trailers:[NSDictionary new] handler:handler];
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
