#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"
#import "library/cc/engine_builder.h"
#include "source/common/protobuf/utility.h"

@implementation NSString (CXX)
- (std::string)toCXXString {
  return std::string([self UTF8String],
                     (int)[self lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
}
@end

@implementation EMOHeaderMatcher
- (Envoy::DirectResponseTesting::HeaderMatcher)toCXX {
  Envoy::DirectResponseTesting::HeaderMatcher result;
  result.name = [self.name toCXXString];
  result.value = [self.value toCXXString];
  switch (self.mode) {
  case EMOMatchModeContains:
    result.mode = Envoy::DirectResponseTesting::MatchMode::Contains;
    break;
  case EMOMatchModeExact:
    result.mode = Envoy::DirectResponseTesting::MatchMode::Exact;
    break;
  case EMOMatchModePrefix:
    result.mode = Envoy::DirectResponseTesting::MatchMode::Prefix;
    break;
  case EMOMatchModeSuffix:
    result.mode = Envoy::DirectResponseTesting::MatchMode::Suffix;
    break;
  }
  return result;
}
@end

@implementation EMORouteMatcher
- (Envoy::DirectResponseTesting::RouteMatcher)toCXX {
  Envoy::DirectResponseTesting::RouteMatcher result;
  result.fullPath = [self.fullPath toCXXString];
  result.pathPrefix = [self.pathPrefix toCXXString];
  std::vector<Envoy::DirectResponseTesting::HeaderMatcher> headers;
  headers.reserve(self.headers.count);
  for (EMOHeaderMatcher *matcher in self.headers) {
    headers.push_back([matcher toCXX]);
  }
  result.headers = headers;
  return result;
}
@end

@implementation EMODirectResponse
- (Envoy::DirectResponseTesting::DirectResponse)toCXX {
  Envoy::DirectResponseTesting::DirectResponse result;
  result.matcher = [self.matcher toCXX];
  result.status = (unsigned int)self.status;
  result.body = [self.body toCXXString];
  absl::flat_hash_map<std::string, std::string> headers;
  NSArray *keys = [self.headers allKeys];
  for (NSString *key in keys) {
    headers[[key toCXXString]] = [[self.headers objectForKey:key] toCXXString];
  }
  result.headers = headers;
  return result;
}
@end

@implementation EnvoyConfiguration

- (instancetype)initWithAdminInterfaceEnabled:(BOOL)adminInterfaceEnabled
                                  grpcStatsDomain:(nullable NSString *)grpcStatsDomain
                            connectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                                dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
                     dnsFailureRefreshSecondsBase:(UInt32)dnsFailureRefreshSecondsBase
                      dnsFailureRefreshSecondsMax:(UInt32)dnsFailureRefreshSecondsMax
                           dnsQueryTimeoutSeconds:(UInt32)dnsQueryTimeoutSeconds
                             dnsMinRefreshSeconds:(UInt32)dnsMinRefreshSeconds
                           dnsPreresolveHostnames:(NSArray<NSString *> *)dnsPreresolveHostnames
                                   enableDNSCache:(BOOL)enableDNSCache
                      dnsCacheSaveIntervalSeconds:(UInt32)dnsCacheSaveIntervalSeconds
                              enableHappyEyeballs:(BOOL)enableHappyEyeballs
                                      enableHttp3:(BOOL)enableHttp3
                          enableGzipDecompression:(BOOL)enableGzipDecompression
                        enableBrotliDecompression:(BOOL)enableBrotliDecompression
                           enableInterfaceBinding:(BOOL)enableInterfaceBinding
                        enableDrainPostDnsRefresh:(BOOL)enableDrainPostDnsRefresh
                    enforceTrustChainVerification:(BOOL)enforceTrustChainVerification
                                        forceIPv6:(BOOL)forceIPv6
              enablePlatformCertificateValidation:(BOOL)enablePlatformCertificateValidation
    h2ConnectionKeepaliveIdleIntervalMilliseconds:
        (UInt32)h2ConnectionKeepaliveIdleIntervalMilliseconds
              h2ConnectionKeepaliveTimeoutSeconds:(UInt32)h2ConnectionKeepaliveTimeoutSeconds
                            maxConnectionsPerHost:(UInt32)maxConnectionsPerHost
                                statsFlushSeconds:(UInt32)statsFlushSeconds
                         streamIdleTimeoutSeconds:(UInt32)streamIdleTimeoutSeconds
                         perTryIdleTimeoutSeconds:(UInt32)perTryIdleTimeoutSeconds
                                       appVersion:(NSString *)appVersion
                                            appId:(NSString *)appId
                                  virtualClusters:(NSArray<NSString *> *)virtualClusters
                                    runtimeGuards:
                                        (NSDictionary<NSString *, NSString *> *)runtimeGuards
                             typedDirectResponses:
                                 (NSArray<EMODirectResponse *> *)typedDirectResponses
                                nativeFilterChain:
                                    (NSArray<EnvoyNativeFilterConfig *> *)nativeFilterChain
                              platformFilterChain:
                                  (NSArray<EnvoyHTTPFilterFactory *> *)httpPlatformFilterFactories
                                  stringAccessors:
                                      (NSDictionary<NSString *, EnvoyStringAccessor *> *)
                                          stringAccessors
                                   keyValueStores:
                                       (NSDictionary<NSString *, id<EnvoyKeyValueStore>> *)
                                           keyValueStores
                                       statsSinks:(NSArray<NSString *> *)statsSinks
                                    rtdsLayerName:(NSString *)rtdsLayerName
                               rtdsTimeoutSeconds:(UInt32)rtdsTimeoutSeconds
                                       adsAddress:(NSString *)adsAddress
                                          adsPort:(UInt32)adsPort
                                      adsJwtToken:(NSString *)adsJwtToken
                       adsJwtTokenLifetimeSeconds:(UInt32)adsJwtTokenLifetimeSeconds
                                  adsSslRootCerts:(NSString *)adsSslRootCerts
                                           nodeId:(NSString *)nodeId
                                       nodeRegion:(NSString *)nodeRegion
                                         nodeZone:(NSString *)nodeZone
                                      nodeSubZone:(NSString *)nodeSubZone
                              cdsResourcesLocator:(NSString *)cdsResourcesLocator
                                cdsTimeoutSeconds:(UInt32)cdsTimeoutSeconds
                                        enableCds:(BOOL)enableCds

{
  self = [super init];
  if (!self) {
    return nil;
  }

  self.adminInterfaceEnabled = adminInterfaceEnabled;
  self.grpcStatsDomain = grpcStatsDomain;
  self.connectTimeoutSeconds = connectTimeoutSeconds;
  self.dnsRefreshSeconds = dnsRefreshSeconds;
  self.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
  self.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
  self.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds;
  self.dnsMinRefreshSeconds = dnsMinRefreshSeconds;
  self.dnsPreresolveHostnames = dnsPreresolveHostnames;
  self.enableDNSCache = enableDNSCache;
  self.dnsCacheSaveIntervalSeconds = dnsCacheSaveIntervalSeconds;
  self.enableHappyEyeballs = enableHappyEyeballs;
  self.enableHttp3 = enableHttp3;
  self.enableGzipDecompression = enableGzipDecompression;
  self.enableBrotliDecompression = enableBrotliDecompression;
  self.enableInterfaceBinding = enableInterfaceBinding;
  self.enableDrainPostDnsRefresh = enableDrainPostDnsRefresh;
  self.enforceTrustChainVerification = enforceTrustChainVerification;
  self.forceIPv6 = forceIPv6;
  self.enablePlatformCertificateValidation = enablePlatformCertificateValidation;
  self.h2ConnectionKeepaliveIdleIntervalMilliseconds =
      h2ConnectionKeepaliveIdleIntervalMilliseconds;
  self.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds;
  self.maxConnectionsPerHost = maxConnectionsPerHost;
  self.statsFlushSeconds = statsFlushSeconds;
  self.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
  self.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds;
  self.appVersion = appVersion;
  self.appId = appId;
  self.virtualClusters = virtualClusters;
  self.runtimeGuards = runtimeGuards;
  self.typedDirectResponses = typedDirectResponses;
  self.nativeFilterChain = nativeFilterChain;
  self.httpPlatformFilterFactories = httpPlatformFilterFactories;
  self.stringAccessors = stringAccessors;
  self.keyValueStores = keyValueStores;
  self.statsSinks = statsSinks;
  self.rtdsLayerName = rtdsLayerName;
  self.rtdsTimeoutSeconds = rtdsTimeoutSeconds;
  self.adsAddress = adsAddress;
  self.adsPort = adsPort;
  self.adsJwtToken = adsJwtToken;
  self.adsJwtTokenLifetimeSeconds = adsJwtTokenLifetimeSeconds;
  self.adsSslRootCerts = adsSslRootCerts;
  self.nodeId = nodeId;
  self.nodeRegion = nodeRegion;
  self.nodeZone = nodeZone;
  self.nodeSubZone = nodeSubZone;
  self.cdsResourcesLocator = cdsResourcesLocator;
  self.cdsTimeoutSeconds = cdsTimeoutSeconds;
  self.enableCds = enableCds;
  self.bootstrapPointer = 0;

  return self;
}

- (Envoy::Platform::EngineBuilder)applyToCXXBuilder {
  Envoy::Platform::EngineBuilder builder;

  for (EnvoyNativeFilterConfig *nativeFilterConfig in
       [self.nativeFilterChain reverseObjectEnumerator]) {
    builder.addNativeFilter(
        /* name */ [nativeFilterConfig.name toCXXString],
        /* typed_config */ [nativeFilterConfig.typedConfig toCXXString]);
  }
  for (EnvoyHTTPFilterFactory *filterFactory in
       [self.httpPlatformFilterFactories reverseObjectEnumerator]) {
    builder.addPlatformFilter([filterFactory.filterName toCXXString]);
  }

#ifdef ENVOY_ENABLE_QUIC
  builder.enableHttp3(self.enableHttp3);
#endif

  builder.enableGzipDecompression(self.enableGzipDecompression);
  builder.enableBrotliDecompression(self.enableBrotliDecompression);

  for (NSString *key in self.runtimeGuards) {
    BOOL value = [[self.runtimeGuards objectForKey:key] isEqualToString:@"true"];
    builder.setRuntimeGuard([key toCXXString], value);
  }

  for (EMODirectResponse *directResponse in self.typedDirectResponses) {
    builder.addDirectResponse([directResponse toCXX]);
  }

  builder.addConnectTimeoutSeconds(self.connectTimeoutSeconds);

  builder.addDnsFailureRefreshSeconds(self.dnsFailureRefreshSecondsBase,
                                      self.dnsFailureRefreshSecondsMax);

  builder.addDnsQueryTimeoutSeconds(self.dnsQueryTimeoutSeconds);
  builder.addDnsMinRefreshSeconds(self.dnsMinRefreshSeconds);
  if (self.dnsPreresolveHostnames.count > 0) {
    std::vector<std::string> hostnames;
    hostnames.reserve(self.dnsPreresolveHostnames.count);
    for (NSString *hostname in self.dnsPreresolveHostnames) {
      hostnames.push_back([hostname toCXXString]);
    }
    builder.addDnsPreresolveHostnames(hostnames);
  }
  builder.enableHappyEyeballs(self.enableHappyEyeballs);
  builder.addDnsRefreshSeconds(self.dnsRefreshSeconds);
  builder.enableDrainPostDnsRefresh(self.enableDrainPostDnsRefresh);
  builder.enableInterfaceBinding(self.enableInterfaceBinding);
  builder.enforceTrustChainVerification(self.enforceTrustChainVerification);
  builder.setForceAlwaysUsev6(self.forceIPv6);
  builder.addH2ConnectionKeepaliveIdleIntervalMilliseconds(
      self.h2ConnectionKeepaliveIdleIntervalMilliseconds);
  builder.addH2ConnectionKeepaliveTimeoutSeconds(self.h2ConnectionKeepaliveTimeoutSeconds);
  builder.addMaxConnectionsPerHost(self.maxConnectionsPerHost);
  builder.setStreamIdleTimeoutSeconds(self.streamIdleTimeoutSeconds);
  builder.setPerTryIdleTimeoutSeconds(self.perTryIdleTimeoutSeconds);
  builder.setAppVersion([self.appVersion toCXXString]);
  builder.setAppId([self.appId toCXXString]);
  builder.setDeviceOs("iOS");
  for (NSString *cluster in self.virtualClusters) {
    builder.addVirtualCluster([cluster toCXXString]);
  }
  builder.enablePlatformCertificatesValidation(self.enablePlatformCertificateValidation);
  builder.enableDnsCache(self.enableDNSCache, self.dnsCacheSaveIntervalSeconds);

#ifdef ENVOY_MOBILE_STATS_REPORTING
  if (self.statsSinks.count > 0) {
    std::vector<std::string> sinks;
    sinks.reserve(self.statsSinks.count);
    for (NSString *sink in self.statsSinks) {
      sinks.push_back([sink toCXXString]);
    }
    builder.addStatsSinks(std::move(sinks));
  }
  builder.addGrpcStatsDomain([self.grpcStatsDomain toCXXString]);
  builder.addStatsFlushSeconds(self.statsFlushSeconds);
#endif

#ifdef ENVOY_GOOGLE_GRPC
  if (self.nodeRegion != nil) {
    builder.setNodeLocality([self.nodeRegion toCXXString], [self.nodeZone toCXXString],
                            [self.nodeSubZone toCXXString]);
  }
  if (self.nodeId != nil) {
    builder.setNodeId([self.nodeId toCXXString]);
  }
  if (self.rtdsLayerName != nil) {
    builder.addRtdsLayer([self.rtdsLayerName toCXXString], self.rtdsTimeoutSeconds);
  }
  if (self.adsAddress != nil) {
    builder.setAggregatedDiscoveryService(
        [self.adsAddress toCXXString], self.adsPort, [self.adsJwtToken toCXXString],
        self.adsJwtTokenLifetimeSeconds, [self.adsSslRootCerts toCXXString]);
  }
  if (self.enableCds) {
    builder.addCdsLayer([self.cdsResourcesLocator toCXXString], self.cdsTimeoutSeconds);
  }
#endif
#ifdef ENVOY_ADMIN_FUNCTIONALITY
  builder.enableAdminInterface(self.adminInterfaceEnabled);
#endif

  return builder;
}

- (std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>)generateBootstrap {
  try {
    Envoy::Platform::EngineBuilder builder = [self applyToCXXBuilder];
    return builder.generateBootstrap();
  } catch (const std::exception &e) {
    NSLog(@"[Envoy] error generating bootstrap: %@", @(e.what()));
    return nullptr;
  }
}

- (NSString *)bootstrapDebugDescription {
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap = [self generateBootstrap];
  return @(bootstrap->ShortDebugString().c_str());
}

@end
