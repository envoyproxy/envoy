#import <Envoy/Envoy-Swift.h>
#import <UIKit/UIKit.h>
#import "Result.h"
#import "ViewController.h"

#pragma mark - Constants

NSString *_CELL_ID = @"cell-id";
NSString *_REQUEST_AUTHORITY = @"api.lyft.com";
NSString *_REQUEST_PATH = @"/ping";
NSString *_REQUEST_SCHEME = @"https";

#pragma mark - ViewController

@interface ViewController ()
@property (nonatomic, strong) id<StreamClient> streamClient;
@property (nonatomic, strong) id<PulseClient> pulseClient;
@property (nonatomic, strong) NSArray<NSString *> *filteredHeaders;
@property (nonatomic, strong) NSMutableArray<Result *> *results;
@property (nonatomic, weak) NSTimer *requestTimer;
@end

@implementation ViewController

#pragma mark - Lifecycle

- (instancetype)init {
  self = [super init];
  if (self) {
    self.filteredHeaders = @[ @"server", @"filter-demo", @"x-envoy-upstream-service-time" ];
    self.results = [NSMutableArray new];
    self.tableView.allowsSelection = NO;
    [self startEnvoy];
  }
  return self;
}

- (void)startEnvoy {
  NSLog(@"starting Envoy...");
  NSError *error;
  EngineBuilder *builder = [[EngineBuilder alloc] init];
  [builder setOnEngineRunningWithClosure:^{
    NSLog(@"Envoy async internal setup completed");
  }];
  id<Engine> engine = [builder buildAndReturnError:&error];
  if (error) {
    NSLog(@"starting Envoy failed: %@", error);
  } else {
    NSLog(@"started Envoy, beginning requests...");
    self.streamClient = [engine streamClient];
    self.pulseClient = [engine pulseClient];
    [self startRequests];
  }
}

- (void)dealloc {
  [self.requestTimer invalidate];
  self.requestTimer = nil;
}

#pragma mark - Requests

- (void)startRequests {
  self.requestTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                       target:self
                                                     selector:@selector(timerFired)
                                                     userInfo:nil
                                                      repeats:YES];
}
- (void)timerFired {
  [self performRequest];
  [self recordStats];
}

- (void)performRequest {
  NSLog(@"starting request to '%@'", _REQUEST_PATH);

  // Note: this request will use an http/1.1 stream for the upstream request.
  // The Swift example uses h2. This is done on purpose to test both paths in end-to-end tests
  // in CI.
  RequestHeadersBuilder *builder = [[RequestHeadersBuilder alloc] initWithMethod:RequestMethodGet
                                                                          scheme:_REQUEST_SCHEME
                                                                       authority:_REQUEST_AUTHORITY
                                                                            path:_REQUEST_PATH];
  [builder addUpstreamHttpProtocol:UpstreamHttpProtocolHttp1];
  RequestHeaders *headers = [builder build];

  __weak ViewController *weakSelf = self;
  StreamPrototype *prototype = [self.streamClient newStreamPrototype];
  [prototype setOnResponseHeadersWithClosure:^(ResponseHeaders *headers, BOOL endStream) {
    int statusCode = [[[headers valueForName:@":status"] firstObject] intValue];
    NSString *message = [NSString stringWithFormat:@"received headers with status %i", statusCode];

    NSMutableString *headerMessage = [NSMutableString new];
    for (NSString *name in headers.allHeaders) {
      if ([self.filteredHeaders containsObject:name]) {
        NSArray<NSString *> *values = headers.allHeaders[name];
        NSString *joined = [values componentsJoinedByString:@", "];
        NSString *pair = [NSString stringWithFormat:@"%@: %@\n", name, joined];
        [headerMessage appendString:pair];
      }
    }

    NSLog(@"%@", message);

    [weakSelf addResponseMessage:message headerMessage:headerMessage error:nil];
  }];
  [prototype setOnErrorWithClosure:^(EnvoyError *error) {
    // TODO: expose attemptCount. https://github.com/lyft/envoy-mobile/issues/823
    NSString *message =
        [NSString stringWithFormat:@"failed within Envoy library %@", error.message];
    NSLog(@"%@", message);
    [weakSelf addResponseMessage:message headerMessage:nil error:message];
  }];

  Stream *stream = [prototype startWithQueue:dispatch_get_main_queue()];
  [stream sendHeaders:headers endStream:YES];
}

- (void)addResponseMessage:(NSString *)message
             headerMessage:(NSString *)headerMessage
                     error:(NSString *)error {
  Result *result = [Result new];
  result.message = message;
  result.headerMessage = headerMessage;
  result.error = error;

  [self.results insertObject:result atIndex:0];
  [self.tableView reloadData];
}

- (void)recordStats {
  Element *elementFoo = [[Element alloc] initWithStringLiteral:@"foo"];
  Element *elementBar = [[Element alloc] initWithStringLiteral:@"bar"];
  Element *elementCounter = [[Element alloc] initWithStringLiteral:@"counter"];
  Element *elementGauge = [[Element alloc] initWithStringLiteral:@"gauge"];
  Element *elementTimer = [[Element alloc] initWithStringLiteral:@"timer"];
  Element *elementDistribution = [[Element alloc] initWithStringLiteral:@"distribution"];
  id<Counter> counter =
      [self.pulseClient counterWithElements:@[ elementFoo, elementBar, elementCounter ]];
  [counter incrementWithCount:1];
  [counter incrementWithCount:5];

  id<Gauge> gauge = [self.pulseClient gaugeWithElements:@[ elementFoo, elementBar, elementGauge ]];
  [gauge setWithValue:5];
  [gauge addWithAmount:10];
  [gauge subWithAmount:1];

  id<Timer> timer = [self.pulseClient timerWithElements:@[ elementFoo, elementBar, elementTimer ]];
  [timer completeWithDurationWithDurationMs:15];
  id<Distribution> distribution =
      [self.pulseClient distributionWithElements:@[ elementFoo, elementBar, elementDistribution ]];
  [distribution recordValueWithValue:15];
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
    cell.textLabel.text = result.message;
    cell.detailTextLabel.text = result.headerMessage;

    cell.textLabel.textColor = [UIColor blackColor];
    cell.detailTextLabel.lineBreakMode = NSLineBreakByWordWrapping;
    cell.detailTextLabel.numberOfLines = 0;
    cell.detailTextLabel.textColor = [UIColor blackColor];
    cell.contentView.backgroundColor = [UIColor whiteColor];
  } else {
    cell.textLabel.text = result.error;
    cell.detailTextLabel.text = nil;

    cell.textLabel.textColor = [UIColor whiteColor];
    cell.detailTextLabel.textColor = [UIColor whiteColor];
    cell.contentView.backgroundColor = [UIColor redColor];
  }

  return cell;
}

@end
