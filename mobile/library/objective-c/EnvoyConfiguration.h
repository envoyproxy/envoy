#import <Foundation/Foundation.h>

@class EMODirectResponse;
@class EnvoyHTTPFilterFactory;
@class EnvoyNativeFilterConfig;
@class EnvoyStringAccessor;
@protocol EnvoyKeyValueStore;

NS_ASSUME_NONNULL_BEGIN

/// Typed configuration that may be used for starting Envoy.
@interface EnvoyConfiguration : NSObject

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
@property (nonatomic, assign) BOOL enableHttp3;
@property (nonatomic, strong) NSDictionary<NSString *, NSNumber *> *quicHints;
@property (nonatomic, assign) BOOL enableGzipDecompression;
@property (nonatomic, assign) BOOL enableBrotliDecompression;
@property (nonatomic, assign) BOOL enableInterfaceBinding;
@property (nonatomic, assign) BOOL enableDrainPostDnsRefresh;
@property (nonatomic, assign) BOOL enforceTrustChainVerification;
@property (nonatomic, assign) BOOL forceIPv6;
@property (nonatomic, assign) BOOL enablePlatformCertificateValidation;
@property (nonatomic, assign) UInt32 h2ConnectionKeepaliveIdleIntervalMilliseconds;
@property (nonatomic, assign) UInt32 h2ConnectionKeepaliveTimeoutSeconds;
@property (nonatomic, assign) UInt32 maxConnectionsPerHost;
@property (nonatomic, assign) UInt32 statsFlushSeconds;
@property (nonatomic, assign) UInt32 streamIdleTimeoutSeconds;
@property (nonatomic, assign) UInt32 perTryIdleTimeoutSeconds;
@property (nonatomic, strong, nullable) NSString *appVersion;
@property (nonatomic, strong, nullable) NSString *appId;
@property (nonatomic, strong) NSDictionary<NSString *, NSString *> *runtimeGuards;
@property (nonatomic, strong) NSArray<EnvoyNativeFilterConfig *> *nativeFilterChain;
@property (nonatomic, strong) NSArray<EnvoyHTTPFilterFactory *> *httpPlatformFilterFactories;
@property (nonatomic, strong) NSDictionary<NSString *, EnvoyStringAccessor *> *stringAccessors;
@property (nonatomic, strong) NSDictionary<NSString *, id<EnvoyKeyValueStore>> *keyValueStores;
@property (nonatomic, strong) NSArray<NSString *> *statsSinks;
@property (nonatomic, strong, nullable) NSString *nodeId;
@property (nonatomic, strong, nullable) NSString *nodeRegion;
@property (nonatomic, strong, nullable) NSString *nodeZone;
@property (nonatomic, strong, nullable) NSString *nodeSubZone;
@property (nonatomic, strong, nullable) NSString *xdsServerAddress;
@property (nonatomic, assign) UInt32 xdsServerPort;
@property (nonatomic, strong, nullable) NSString *xdsAuthHeader;
@property (nonatomic, strong, nullable) NSString *xdsAuthToken;
@property (nonatomic, strong, nullable) NSString *xdsJwtToken;
@property (nonatomic, assign) UInt32 xdsJwtTokenLifetimeSeconds;
@property (nonatomic, strong, nullable) NSString *xdsSslRootCerts;
@property (nonatomic, strong, nullable) NSString *xdsSni;
@property (nonatomic, strong, nullable) NSString *rtdsResourceName;
@property (nonatomic, assign) UInt32 rtdsTimeoutSeconds;
@property (nonatomic, assign) BOOL enableCds;
@property (nonatomic, strong, nullable) NSString *cdsResourcesLocator;
@property (nonatomic, assign) UInt32 cdsTimeoutSeconds;
@property (nonatomic, assign) intptr_t bootstrapPointer;

/**
 Create a new instance of the configuration.
 */
- (instancetype)initWithGrpcStatsDomain:(nullable NSString *)grpcStatsDomain
                            connectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                                dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
                     dnsFailureRefreshSecondsBase:(UInt32)dnsFailureRefreshSecondsBase
                      dnsFailureRefreshSecondsMax:(UInt32)dnsFailureRefreshSecondsMax
                           dnsQueryTimeoutSeconds:(UInt32)dnsQueryTimeoutSeconds
                             dnsMinRefreshSeconds:(UInt32)dnsMinRefreshSeconds
                           dnsPreresolveHostnames:(NSArray<NSString *> *)dnsPreresolveHostnames
                                   enableDNSCache:(BOOL)enableDNSCache
                      dnsCacheSaveIntervalSeconds:(UInt32)dnsCacheSaveIntervalSeconds
                                      enableHttp3:(BOOL)enableHttp3
                                        quicHints:(NSDictionary<NSString *, NSNumber *> *)quicHints
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
                                           keyValueStores
                                       statsSinks:(NSArray<NSString *> *)statsSinks
                                           nodeId:(nullable NSString *)nodeId
                                       nodeRegion:(nullable NSString *)nodeRegion
                                         nodeZone:(nullable NSString *)nodeZone
                                      nodeSubZone:(nullable NSString *)nodeSubZone
                                 xdsServerAddress:(nullable NSString *)xdsServerAddress
                                    xdsServerPort:(UInt32)xdsServerPort
                                    xdsAuthHeader:(nullable NSString *)xdsAuthHeader
                                     xdsAuthToken:(nullable NSString *)xdsAuthToken
                                      xdsJwtToken:(nullable NSString *)xdsJwtToken
                       xdsJwtTokenLifetimeSeconds:(UInt32)xdsJwtTokenLifetimeSeconds
                                  xdsSslRootCerts:(nullable NSString *)xdsSslRootCerts
                                           xdsSni:(nullable NSString *)xdsSni
                                 rtdsResourceName:(nullable NSString *)rtdsResourceName
                               rtdsTimeoutSeconds:(UInt32)rtdsTimeoutSeconds
                                        enableCds:(BOOL)enableCds
                              cdsResourcesLocator:(nullable NSString *)cdsResourcesLocator
                                cdsTimeoutSeconds:(UInt32)cdsTimeoutSeconds;

/**
 Generate a string description of the C++ Envoy bootstrap from this configuration.
 For testing purposes only.
 */
- (NSString *)bootstrapDebugDescription;

@end

NS_ASSUME_NONNULL_END
