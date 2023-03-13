#import <Foundation/Foundation.h>

@class EMODirectResponse;
@class EnvoyHTTPFilterFactory;
@class EnvoyNativeFilterConfig;
@class EnvoyStringAccessor;
@protocol EnvoyKeyValueStore;

NS_ASSUME_NONNULL_BEGIN

/// Typed configuration that may be used for starting Envoy.
@interface EnvoyConfiguration : NSObject

@property (nonatomic, assign) BOOL adminInterfaceEnabled;
@property (nonatomic, strong, nullable) NSString *grpcStatsDomain;
@property (nonatomic, assign) UInt32 connectTimeoutSeconds;
@property (nonatomic, assign) UInt32 dnsFailureRefreshSecondsBase;
@property (nonatomic, assign) UInt32 dnsFailureRefreshSecondsMax;
@property (nonatomic, assign) UInt32 dnsQueryTimeoutSeconds;
@property (nonatomic, assign) UInt32 dnsMinRefreshSeconds;
@property (nonatomic, strong) NSArray<NSString *> *dnsPreresolveHostnames;
@property (nonatomic, assign) UInt32 dnsRefreshSeconds;
@property (nonatomic, assign) BOOL enableDNSCache;
@property (nonatomic, assign) UInt32 dnsCacheSaveIntervalSeconds;
@property (nonatomic, assign) BOOL enableHappyEyeballs;
@property (nonatomic, assign) BOOL enableHttp3;
@property (nonatomic, assign) BOOL enableGzipDecompression;
@property (nonatomic, assign) BOOL enableBrotliDecompression;
@property (nonatomic, assign) BOOL enableInterfaceBinding;
@property (nonatomic, assign) BOOL enableDrainPostDnsRefresh;
@property (nonatomic, assign) BOOL enforceTrustChainVerification;
@property (nonatomic, assign) BOOL forceIPv6;
@property (nonatomic, assign) UInt32 h2ConnectionKeepaliveIdleIntervalMilliseconds;
@property (nonatomic, assign) UInt32 h2ConnectionKeepaliveTimeoutSeconds;
@property (nonatomic, assign) UInt32 maxConnectionsPerHost;
@property (nonatomic, assign) UInt32 statsFlushSeconds;
@property (nonatomic, assign) UInt32 streamIdleTimeoutSeconds;
@property (nonatomic, assign) UInt32 perTryIdleTimeoutSeconds;
@property (nonatomic, strong) NSString *appVersion;
@property (nonatomic, strong) NSString *appId;
@property (nonatomic, strong) NSArray<NSString *> *virtualClusters;
@property (nonatomic, strong) NSDictionary<NSString *, NSString *> *runtimeGuards;
@property (nonatomic, strong) NSArray<EMODirectResponse *> *typedDirectResponses;
@property (nonatomic, strong) NSArray<EnvoyNativeFilterConfig *> *nativeFilterChain;
@property (nonatomic, strong) NSArray<EnvoyHTTPFilterFactory *> *httpPlatformFilterFactories;
@property (nonatomic, strong) NSDictionary<NSString *, EnvoyStringAccessor *> *stringAccessors;
@property (nonatomic, strong) NSDictionary<NSString *, id<EnvoyKeyValueStore>> *keyValueStores;
@property (nonatomic, strong) NSArray<NSString *> *statsSinks;
@property (nonatomic, strong) NSString *rtdsLayerName;
@property (nonatomic, assign) UInt32 rtdsTimeoutSeconds;
@property (nonatomic, strong) NSString *adsAddress;
@property (nonatomic, assign) UInt32 adsPort;
@property (nonatomic, strong) NSString *adsJwtToken;
@property (nonatomic, assign) UInt32 adsJwtTokenLifetimeSeconds;
@property (nonatomic, strong) NSString *adsSslRootCerts;
@property (nonatomic, strong) NSString *nodeId;
@property (nonatomic, strong) NSString *nodeRegion;
@property (nonatomic, strong) NSString *nodeZone;
@property (nonatomic, strong) NSString *nodeSubZone;
/**
 Create a new instance of the configuration.
 */
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
                                      nodeSubZone:(NSString *)nodeSubZone;

/**
 Generate a string description of the C++ Envoy bootstrap from this configuration.
 For testing purposes only.
 */
- (NSString *)bootstrapDebugDescription;

@end

NS_ASSUME_NONNULL_END
