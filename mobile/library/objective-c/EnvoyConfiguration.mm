#import "library/objective-c/EnvoyEngine.h"

#import "library/cc/engine_builder.h"
#import "library/cc/direct_response_testing.h"
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

- (instancetype)initWithConnectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                                dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
                     dnsFailureRefreshSecondsBase:(UInt32)dnsFailureRefreshSecondsBase
                      dnsFailureRefreshSecondsMax:(UInt32)dnsFailureRefreshSecondsMax
                           dnsQueryTimeoutSeconds:(UInt32)dnsQueryTimeoutSeconds
                             dnsMinRefreshSeconds:(UInt32)dnsMinRefreshSeconds
                           dnsPreresolveHostnames:(NSArray<NSString *> *)dnsPreresolveHostnames
                                   enableDNSCache:(BOOL)enableDNSCache
                      dnsCacheSaveIntervalSeconds:(UInt32)dnsCacheSaveIntervalSeconds
                                    dnsNumRetries:(NSInteger)dnsNumRetries
                                      enableHttp3:(BOOL)enableHttp3
                                        quicHints:(NSDictionary<NSString *, NSNumber *> *)quicHints
                            quicCanonicalSuffixes:(NSArray<NSString *> *)quicCanonicalSuffixes
                          enableGzipDecompression:(BOOL)enableGzipDecompression
                        enableBrotliDecompression:(BOOL)enableBrotliDecompression
                           enableInterfaceBinding:(BOOL)enableInterfaceBinding
                        enableDrainPostDnsRefresh:(BOOL)enableDrainPostDnsRefresh
                    enforceTrustChainVerification:(BOOL)enforceTrustChainVerification
                                        forceIPv6:(BOOL)forceIPv6
              enablePlatformCertificateValidation:(BOOL)enablePlatformCertificateValidation
                                   upstreamTlsSni:(nullable NSString *)upstreamTlsSni
                       respectSystemProxySettings:(BOOL)respectSystemProxySettings
    h2ConnectionKeepaliveIdleIntervalMilliseconds:
        (UInt32)h2ConnectionKeepaliveIdleIntervalMilliseconds
              h2ConnectionKeepaliveTimeoutSeconds:(UInt32)h2ConnectionKeepaliveTimeoutSeconds
                            maxConnectionsPerHost:(UInt32)maxConnectionsPerHost
                         streamIdleTimeoutSeconds:(UInt32)streamIdleTimeoutSeconds
                         perTryIdleTimeoutSeconds:(UInt32)perTryIdleTimeoutSeconds
                                       appVersion:(NSString *)appVersion
                                            appId:(NSString *)appId
                                    runtimeGuards:
                                        (NSDictionary<NSString *, NSString *> *)runtimeGuards
                                nativeFilterChain:
                                    (NSArray<EnvoyNativeFilterConfig *> *)nativeFilterChain
                              platformFilterChain:
                                  (NSArray<EnvoyHTTPFilterFactory *> *)httpPlatformFilterFactories
                                  stringAccessors:
                                      (NSDictionary<NSString *, EnvoyStringAccessor *> *)
                                          stringAccessors
                                   keyValueStores:
                                       (NSDictionary<NSString *, id<EnvoyKeyValueStore>> *)
                                           keyValueStores {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.connectTimeoutSeconds = connectTimeoutSeconds;
  self.dnsRefreshSeconds = dnsRefreshSeconds;
  self.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
  self.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
  self.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds;
  self.dnsMinRefreshSeconds = dnsMinRefreshSeconds;
  self.dnsPreresolveHostnames = dnsPreresolveHostnames;
  self.enableDNSCache = enableDNSCache;
  self.dnsCacheSaveIntervalSeconds = dnsCacheSaveIntervalSeconds;
  self.dnsNumRetries = dnsNumRetries;
  self.enableHttp3 = enableHttp3;
  self.quicHints = quicHints;
  self.quicCanonicalSuffixes = quicCanonicalSuffixes;
  self.enableGzipDecompression = enableGzipDecompression;
  self.enableBrotliDecompression = enableBrotliDecompression;
  self.enableInterfaceBinding = enableInterfaceBinding;
  self.enableDrainPostDnsRefresh = enableDrainPostDnsRefresh;
  self.enforceTrustChainVerification = enforceTrustChainVerification;
  self.forceIPv6 = forceIPv6;
  self.enablePlatformCertificateValidation = enablePlatformCertificateValidation;
  self.upstreamTlsSni = upstreamTlsSni;
  self.respectSystemProxySettings = respectSystemProxySettings;
  self.h2ConnectionKeepaliveIdleIntervalMilliseconds =
      h2ConnectionKeepaliveIdleIntervalMilliseconds;
  self.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds;
  self.maxConnectionsPerHost = maxConnectionsPerHost;
  self.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
  self.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds;
  self.appVersion = appVersion;
  self.appId = appId;
  self.runtimeGuards = runtimeGuards;
  self.nativeFilterChain = nativeFilterChain;
  self.httpPlatformFilterFactories = httpPlatformFilterFactories;
  self.stringAccessors = stringAccessors;
  self.keyValueStores = keyValueStores;
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

  builder.enableHttp3(self.enableHttp3);
  for (NSString *host in self.quicHints) {
    builder.addQuicHint([host toCXXString], [[self.quicHints objectForKey:host] intValue]);
  }
  for (NSString *suffix in self.quicCanonicalSuffixes) {
    builder.addQuicCanonicalSuffix([suffix toCXXString]);
  }

  builder.enableGzipDecompression(self.enableGzipDecompression);
  builder.enableBrotliDecompression(self.enableBrotliDecompression);

  for (NSString *key in self.runtimeGuards) {
    BOOL value = [[self.runtimeGuards objectForKey:key] isEqualToString:@"true"];
    builder.addRuntimeGuard([key toCXXString], value);
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
  builder.enablePlatformCertificatesValidation(self.enablePlatformCertificateValidation);
  builder.respectSystemProxySettings(self.respectSystemProxySettings);
  builder.enableDnsCache(self.enableDNSCache, self.dnsCacheSaveIntervalSeconds);
  if (self.dnsNumRetries >= 0) {
    builder.setDnsNumRetries(self.dnsNumRetries);
  }
  if (self.upstreamTlsSni != nil) {
    builder.setUpstreamTlsSni([self.upstreamTlsSni toCXXString]);
  }

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
