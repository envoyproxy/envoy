package io.envoyproxy.envoymobile.engine;

import java.util.Collections;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.lang.StringBuilder;
import javax.annotation.Nullable;

import io.envoyproxy.envoymobile.engine.VirtualClusterConfig;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore;
import io.envoyproxy.envoymobile.engine.JniLibrary;

/* Typed configuration that may be used for starting Envoy. */
public class EnvoyConfiguration {
  // Peer certificate verification mode.
  // Must match the CertificateValidationContext.TrustChainVerification proto enum.
  public enum TrustChainVerification {
    // Perform default certificate verification (e.g., against CA / verification lists)
    VERIFY_TRUST_CHAIN,
    // Connections where the certificate fails verification will be permitted.
    // For HTTP connections, the result of certificate verification can be used in route matching.
    // Used for testing.
    ACCEPT_UNTRUSTED;
  }

  public final Boolean adminInterfaceEnabled;
  public final String grpcStatsDomain;
  public final Integer connectTimeoutSeconds;
  public final Integer dnsRefreshSeconds;
  public final Integer dnsFailureRefreshSecondsBase;
  public final Integer dnsFailureRefreshSecondsMax;
  public final Integer dnsQueryTimeoutSeconds;
  public final Integer dnsMinRefreshSeconds;
  public final List<String> dnsPreresolveHostnames;
  public final Boolean enableDNSCache;
  public final Integer dnsCacheSaveIntervalSeconds;
  public final Boolean enableDrainPostDnsRefresh;
  public final Boolean enableHttp3;
  public final Boolean enableGzipDecompression;
  public final Boolean enableBrotliDecompression;
  public final Boolean enableSocketTagging;
  public final Boolean enableHappyEyeballs;
  public final Boolean enableInterfaceBinding;
  public final Integer h2ConnectionKeepaliveIdleIntervalMilliseconds;
  public final Integer h2ConnectionKeepaliveTimeoutSeconds;
  public final Integer maxConnectionsPerHost;
  public final List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories;
  public final Integer statsFlushSeconds;
  public final Integer streamIdleTimeoutSeconds;
  public final Integer perTryIdleTimeoutSeconds;
  public final String appVersion;
  public final String appId;
  public final TrustChainVerification trustChainVerification;
  public final List<String> legacyVirtualClusters;
  public final List<VirtualClusterConfig> virtualClusters;
  public final List<EnvoyNativeFilterConfig> nativeFilterChain;
  public final Map<String, EnvoyStringAccessor> stringAccessors;
  public final Map<String, EnvoyKeyValueStore> keyValueStores;
  public final List<String> statSinks;
  public final Map<String, String> runtimeGuards;
  public final Boolean enablePlatformCertificatesValidation;
  public final Boolean enableSkipDNSLookupForProxiedRequests;
  public final String rtdsLayerName;
  public final Integer rtdsTimeoutSeconds;
  public final String adsAddress;
  public final Integer adsPort;
  public final String adsToken;
  public final Integer adsTokenLifetime;
  public final String adsRootCerts;
  public final String nodeId;
  public final String nodeRegion;
  public final String nodeZone;
  public final String nodeSubZone;
  public final String cdsResourcesLocator;
  public final Integer cdsTimeoutSeconds;
  public final Boolean enableCds;

  private static final Pattern UNRESOLVED_KEY_PATTERN = Pattern.compile("\\{\\{ (.+) \\}\\}");

  /**
   * Create a new instance of the configuration.
   *
   * @param adminInterfaceEnabled                         whether admin interface should be enabled
   *     or not.
   * @param grpcStatsDomain                               the domain to flush stats to.
   * @param connectTimeoutSeconds                         timeout for new network connections to
   *     hosts in
   *                                                      the cluster.
   * @param dnsRefreshSeconds                             default rate in seconds at which to
   *     refresh DNS.
   * @param dnsFailureRefreshSecondsBase                  base rate in seconds to refresh DNS on
   *     failure.
   * @param dnsFailureRefreshSecondsMax                   max rate in seconds to refresh DNS on
   *     failure.
   * @param dnsQueryTimeoutSeconds                        rate in seconds to timeout DNS queries.
   * @param dnsMinRefreshSeconds                          minimum rate in seconds at which to
   *     refresh DNS.
   * @param dnsPreresolveHostnames                        hostnames to preresolve on Envoy Client
   *     construction.
   * @param enableDNSCache                                whether to enable DNS cache.
   * @param dnsCacheSaveIntervalSeconds                   the interval at which to save results to
   *     the configured key value store.
   * @param enableDrainPostDnsRefresh                     whether to drain connections after soft
   *     DNS refresh.
   * @param enableHttp3                                   whether to enable experimental support for
   *     HTTP/3 (QUIC).
   * @param enableGzipDecompression                       whether to enable response gzip
   *     decompression.
   *     compression.
   * @param enableBrotliDecompression                     whether to enable response brotli
   *     decompression.
   *     compression.
   * @param enableSocketTagging                           whether to enable socket tagging.
   * @param enableHappyEyeballs                           whether to enable RFC 6555 handling for
   *     IPv4/IPv6.
   * @param enableInterfaceBinding                        whether to allow interface binding.
   * @param h2ConnectionKeepaliveIdleIntervalMilliseconds rate in milliseconds seconds to send h2
   *                                                      pings on stream creation.
   * @param h2ConnectionKeepaliveTimeoutSeconds           rate in seconds to timeout h2 pings.
   * @param maxConnectionsPerHost                         maximum number of connections to open to a
   *                                                      single host.
   * @param statsFlushSeconds                             interval at which to flush Envoy stats.
   * @param streamIdleTimeoutSeconds                      idle timeout for HTTP streams.
   * @param perTryIdleTimeoutSeconds                      per try idle timeout for HTTP streams.
   * @param appVersion                                    the App Version of the App using this
   *     Envoy Client.
   * @param appId                                         the App ID of the App using this Envoy
   *     Client.
   * @param trustChainVerification                        whether to mute TLS Cert verification -
   *     for tests.
   * @param legacyVirtualClusters                               the JSON list of virtual cluster
   *     configs.
   * @param virtualClusters                          the structured list of virtual cluster
   *     configs.
   * @param nativeFilterChain                             the configuration for native filters.
   * @param httpPlatformFilterFactories                   the configuration for platform filters.
   * @param stringAccessors                               platform string accessors to register.
   * @param keyValueStores                                platform key-value store implementations.
   * @param enableSkipDNSLookupForProxiedRequests         whether to skip waiting on DNS response
   *     for proxied requests.
   * @param enablePlatformCertificatesValidation          whether to use the platform verifier.
   * @param rtdsLayerName                                 the RTDS layer name for this client.
   * @param rtdsTimeoutSeconds                            the timeout for RTDS fetches.
   * @param adsAddress                                    the address for the ADS server.
   * @param adsPort                                       the port for the ADS server.
   * @param adsToken                                      the token to use for authenticating with
   *                                                      the ADS server.
   * @param adsTokenLifetime                              the lifetime of the ADS token.
   * @param adsRootCerts                                  the root certificates to use for
   *     validating the ADS server.
   * @param nodeId                                        the node ID to use for the ADS server.
   * @param nodeRegion                                    the node region to use for the ADS server.
   * @param nodeZone                                      the node zone to use for the ADS server.
   * @param nodeSubZone                                   the node sub-zone to use for the ADS
   *     server.
   * @param cdsResourcesLocator                           the resources locator for CDS.
   * @param cdsTimeoutSeconds                             the timeout for CDS fetches.
   * @param enableCds                                     enables CDS, used because all CDS params
   *     could be empty.
   */
  public EnvoyConfiguration(
      boolean adminInterfaceEnabled, String grpcStatsDomain, int connectTimeoutSeconds,
      int dnsRefreshSeconds, int dnsFailureRefreshSecondsBase, int dnsFailureRefreshSecondsMax,
      int dnsQueryTimeoutSeconds, int dnsMinRefreshSeconds, List<String> dnsPreresolveHostnames,
      boolean enableDNSCache, int dnsCacheSaveIntervalSeconds, boolean enableDrainPostDnsRefresh,
      boolean enableHttp3, boolean enableGzipDecompression, boolean enableBrotliDecompression,
      boolean enableSocketTagging, boolean enableHappyEyeballs, boolean enableInterfaceBinding,
      int h2ConnectionKeepaliveIdleIntervalMilliseconds, int h2ConnectionKeepaliveTimeoutSeconds,
      int maxConnectionsPerHost, int statsFlushSeconds, int streamIdleTimeoutSeconds,
      int perTryIdleTimeoutSeconds, String appVersion, String appId,
      TrustChainVerification trustChainVerification, List<String> legacyVirtualClusters,
      List<VirtualClusterConfig> virtualClusters, List<EnvoyNativeFilterConfig> nativeFilterChain,
      List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories,
      Map<String, EnvoyStringAccessor> stringAccessors,
      Map<String, EnvoyKeyValueStore> keyValueStores, List<String> statSinks,
      Map<String, Boolean> runtimeGuards, Boolean enableSkipDNSLookupForProxiedRequests,
      boolean enablePlatformCertificatesValidation, String rtdsLayerName,
      Integer rtdsTimeoutSeconds, String adsAddress, Integer adsPort, String adsToken,
      Integer adsTokenLifetime, String adsRootCerts, String nodeId, String nodeRegion,
      String nodeZone, String nodeSubZone, String cdsResourcesLocator, Integer cdsTimeoutSeconds,
      boolean enableCds) {
    JniLibrary.load();
    this.adminInterfaceEnabled = adminInterfaceEnabled;
    this.grpcStatsDomain = grpcStatsDomain;
    this.connectTimeoutSeconds = connectTimeoutSeconds;
    this.dnsRefreshSeconds = dnsRefreshSeconds;
    this.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
    this.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
    this.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds;
    this.dnsMinRefreshSeconds = dnsMinRefreshSeconds;
    this.dnsPreresolveHostnames = dnsPreresolveHostnames;
    this.enableDNSCache = enableDNSCache;
    this.dnsCacheSaveIntervalSeconds = dnsCacheSaveIntervalSeconds;
    this.enableDrainPostDnsRefresh = enableDrainPostDnsRefresh;
    this.enableHttp3 = enableHttp3;
    this.enableGzipDecompression = enableGzipDecompression;
    this.enableBrotliDecompression = enableBrotliDecompression;
    this.enableSocketTagging = enableSocketTagging;
    this.enableHappyEyeballs = enableHappyEyeballs;
    this.enableInterfaceBinding = enableInterfaceBinding;
    this.h2ConnectionKeepaliveIdleIntervalMilliseconds =
        h2ConnectionKeepaliveIdleIntervalMilliseconds;
    this.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds;
    this.maxConnectionsPerHost = maxConnectionsPerHost;
    this.statsFlushSeconds = statsFlushSeconds;
    this.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
    this.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds;
    this.appVersion = appVersion;
    this.appId = appId;
    this.trustChainVerification = trustChainVerification;
    this.legacyVirtualClusters = legacyVirtualClusters;
    this.virtualClusters = virtualClusters;
    int index = 0;
    // Insert in this order to preserve prior ordering constraints.
    for (EnvoyHTTPFilterFactory filterFactory : httpPlatformFilterFactories) {
      String config =
          "{'@type': type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge, platform_filter_name: " +
          filterFactory.getFilterName() + "}";
      EnvoyNativeFilterConfig ins =
          new EnvoyNativeFilterConfig("envoy.filters.http.platform_bridge", config);
      nativeFilterChain.add(index++, ins);
    }
    this.nativeFilterChain = nativeFilterChain;

    this.httpPlatformFilterFactories = httpPlatformFilterFactories;
    this.stringAccessors = stringAccessors;
    this.keyValueStores = keyValueStores;
    this.statSinks = statSinks;

    this.runtimeGuards = new HashMap<String, String>();
    for (Map.Entry<String, Boolean> guardAndValue : runtimeGuards.entrySet()) {
      this.runtimeGuards.put(guardAndValue.getKey(), String.valueOf(guardAndValue.getValue()));
    }
    this.enablePlatformCertificatesValidation = enablePlatformCertificatesValidation;
    this.enableSkipDNSLookupForProxiedRequests = enableSkipDNSLookupForProxiedRequests;
    this.rtdsLayerName = rtdsLayerName;
    this.rtdsTimeoutSeconds = rtdsTimeoutSeconds;
    this.adsAddress = adsAddress;
    this.adsPort = adsPort;
    this.adsToken = adsToken;
    this.adsTokenLifetime = adsTokenLifetime;
    this.adsRootCerts = adsRootCerts;
    this.nodeId = nodeId;
    this.nodeRegion = nodeRegion;
    this.nodeZone = nodeZone;
    this.nodeSubZone = nodeSubZone;
    this.cdsResourcesLocator = cdsResourcesLocator;
    this.cdsTimeoutSeconds = cdsTimeoutSeconds;
    this.enableCds = enableCds;
  }

  public long createBootstrap() {
    Boolean enforceTrustChainVerification =
        trustChainVerification == EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;
    List<EnvoyNativeFilterConfig> reverseFilterChain = new ArrayList<>(nativeFilterChain);
    Collections.reverse(reverseFilterChain);

    byte[][] filter_chain = JniBridgeUtility.toJniBytes(reverseFilterChain);
    byte[][] clusters_legacy = JniBridgeUtility.stringsToJniBytes(legacyVirtualClusters);
    byte[][] stats_sinks = JniBridgeUtility.stringsToJniBytes(statSinks);
    byte[][] dns_preresolve = JniBridgeUtility.stringsToJniBytes(dnsPreresolveHostnames);
    byte[][] runtime_guards = JniBridgeUtility.mapToJniBytes(runtimeGuards);
    byte[][] clusters = JniBridgeUtility.clusterConfigToJniBytes(virtualClusters);

    return JniLibrary.createBootstrap(
        grpcStatsDomain, adminInterfaceEnabled, connectTimeoutSeconds, dnsRefreshSeconds,
        dnsFailureRefreshSecondsBase, dnsFailureRefreshSecondsMax, dnsQueryTimeoutSeconds,
        dnsMinRefreshSeconds, dns_preresolve, enableDNSCache, dnsCacheSaveIntervalSeconds,
        enableDrainPostDnsRefresh, enableHttp3, enableGzipDecompression, enableBrotliDecompression,
        enableSocketTagging, enableHappyEyeballs, enableInterfaceBinding,
        h2ConnectionKeepaliveIdleIntervalMilliseconds, h2ConnectionKeepaliveTimeoutSeconds,
        maxConnectionsPerHost, statsFlushSeconds, streamIdleTimeoutSeconds,
        perTryIdleTimeoutSeconds, appVersion, appId, enforceTrustChainVerification, clusters_legacy,
        clusters, filter_chain, stats_sinks, enablePlatformCertificatesValidation,
        enableSkipDNSLookupForProxiedRequests, runtime_guards, rtdsLayerName, rtdsTimeoutSeconds,
        adsAddress, adsPort, adsToken, adsTokenLifetime, adsRootCerts, nodeId, nodeRegion, nodeZone,
        nodeSubZone, cdsResourcesLocator, cdsTimeoutSeconds, enableCds);
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException(String unresolvedKey) {
      super("Unresolved template key: " + unresolvedKey);
    }
  }
}
