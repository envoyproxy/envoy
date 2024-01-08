package io.envoyproxy.envoymobile.engine;

import com.google.protobuf.Struct;
import java.util.Collections;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;
import java.util.regex.Pattern;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore;

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
  public final String http3ConnectionOptions;
  public final String http3ClientConnectionOptions;
  public final Map<String, String> quicHints;
  public final List<String> quicCanonicalSuffixes;
  public final Boolean enableGzipDecompression;
  public final Boolean enableBrotliDecompression;
  public final Boolean enableSocketTagging;
  public final Boolean enableInterfaceBinding;
  public final Integer h2ConnectionKeepaliveIdleIntervalMilliseconds;
  public final Integer h2ConnectionKeepaliveTimeoutSeconds;
  public final Integer maxConnectionsPerHost;
  public final List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories;
  public final Integer streamIdleTimeoutSeconds;
  public final Integer perTryIdleTimeoutSeconds;
  public final String appVersion;
  public final String appId;
  public final TrustChainVerification trustChainVerification;
  public final List<EnvoyNativeFilterConfig> nativeFilterChain;
  public final Map<String, EnvoyStringAccessor> stringAccessors;
  public final Map<String, EnvoyKeyValueStore> keyValueStores;
  public final Map<String, String> runtimeGuards;
  public final Boolean enablePlatformCertificatesValidation;
  public final String rtdsResourceName;
  public final Integer rtdsTimeoutSeconds;
  public final String xdsAddress;
  public final Integer xdsPort;
  public final Map<String, String> xdsGrpcInitialMetadata;
  public final String xdsRootCerts;
  public final String nodeId;
  public final String nodeRegion;
  public final String nodeZone;
  public final String nodeSubZone;
  public final Struct nodeMetadata;
  public final String cdsResourcesLocator;
  public final Integer cdsTimeoutSeconds;
  public final Boolean enableCds;

  private static final Pattern UNRESOLVED_KEY_PATTERN = Pattern.compile("\\{\\{ (.+) \\}\\}");

  /**
   * Create a new instance of the configuration.
   *
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
   * @param http3ConnectionOptions                        connection options to be used in HTTP/3.
   * @param http3ClientConnectionOptions                  client connection options to be used in
   *     HTTP/3.
   * @param quicHints                                     A list of host port pairs that's known
   *     to speak QUIC.
   * @param quicCanonicalSuffixes                         A list of canonical suffixes that are
   *     known to speak QUIC.
   * @param enableGzipDecompression                       whether to enable response gzip
   *     decompression.
   * @param enableBrotliDecompression                     whether to enable response brotli
   *     decompression.
   * @param enableSocketTagging                           whether to enable socket tagging.
   * @param enableInterfaceBinding                        whether to allow interface binding.
   * @param h2ConnectionKeepaliveIdleIntervalMilliseconds rate in milliseconds seconds to send h2
   *                                                      pings on stream creation.
   * @param h2ConnectionKeepaliveTimeoutSeconds           rate in seconds to timeout h2 pings.
   * @param maxConnectionsPerHost                         maximum number of connections to open to a
   *                                                      single host.
   * @param streamIdleTimeoutSeconds                      idle timeout for HTTP streams.
   * @param perTryIdleTimeoutSeconds                      per try idle timeout for HTTP streams.
   * @param appVersion                                    the App Version of the App using this
   *     Envoy Client.
   * @param appId                                         the App ID of the App using this Envoy
   *     Client.
   * @param trustChainVerification                        whether to mute TLS Cert verification -
   *     for tests.
   * @param nativeFilterChain                             the configuration for native filters.
   * @param httpPlatformFilterFactories                   the configuration for platform filters.
   * @param stringAccessors                               platform string accessors to register.
   * @param keyValueStores                                platform key-value store implementations.
   * @param enablePlatformCertificatesValidation          whether to use the platform verifier.
   * @param rtdsResourceName                                 the RTDS layer name for this client.
   * @param rtdsTimeoutSeconds                            the timeout for RTDS fetches.
   * @param xdsAddress                                    the address for the xDS management server.
   * @param xdsPort                                       the port for the xDS server.
   * @param xdsGrpcInitialMetadata                        The Headers (as key/value pairs) that must
   *                                                      be included in the xDs gRPC stream's
   *                                                      initial metadata (as HTTP headers).
   * @param xdsRootCerts                                  the root certificates to use for the TLS
   *                                                      handshake during connection establishment
   *                                                      with the xDS management server.
   * @param nodeId                                        the node ID in the Node metadata.
   * @param nodeRegion                                    the node region in the Node metadata.
   * @param nodeZone                                      the node zone in the Node metadata.
   * @param nodeSubZone                                   the node sub-zone in the Node metadata.
   * @param nodeMetadata                                  the node metadata.
   * @param cdsResourcesLocator                           the resources locator for CDS.
   * @param cdsTimeoutSeconds                             the timeout for CDS fetches.
   * @param enableCds                                     enables CDS, used because all CDS params
   *     could be empty.
   */
  public EnvoyConfiguration(
      int connectTimeoutSeconds, int dnsRefreshSeconds, int dnsFailureRefreshSecondsBase,
      int dnsFailureRefreshSecondsMax, int dnsQueryTimeoutSeconds, int dnsMinRefreshSeconds,
      List<String> dnsPreresolveHostnames, boolean enableDNSCache, int dnsCacheSaveIntervalSeconds,
      boolean enableDrainPostDnsRefresh, boolean enableHttp3, String http3ConnectionOptions,
      String http3ClientConnectionOptions, Map<String, Integer> quicHints,
      List<String> quicCanonicalSuffixes, boolean enableGzipDecompression,
      boolean enableBrotliDecompression, boolean enableSocketTagging,
      boolean enableInterfaceBinding, int h2ConnectionKeepaliveIdleIntervalMilliseconds,
      int h2ConnectionKeepaliveTimeoutSeconds, int maxConnectionsPerHost,
      int streamIdleTimeoutSeconds, int perTryIdleTimeoutSeconds, String appVersion, String appId,
      TrustChainVerification trustChainVerification,
      List<EnvoyNativeFilterConfig> nativeFilterChain,
      List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories,
      Map<String, EnvoyStringAccessor> stringAccessors,
      Map<String, EnvoyKeyValueStore> keyValueStores, Map<String, Boolean> runtimeGuards,
      boolean enablePlatformCertificatesValidation, String rtdsResourceName,
      Integer rtdsTimeoutSeconds, String xdsAddress, Integer xdsPort,
      Map<String, String> xdsGrpcInitialMetadata, String xdsRootCerts, String nodeId,
      String nodeRegion, String nodeZone, String nodeSubZone, Struct nodeMetadata,
      String cdsResourcesLocator, Integer cdsTimeoutSeconds, boolean enableCds) {
    JniLibrary.load();
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
    this.http3ConnectionOptions = http3ConnectionOptions;
    this.http3ClientConnectionOptions = http3ClientConnectionOptions;
    this.quicHints = new HashMap<>();
    for (Map.Entry<String, Integer> hostAndPort : quicHints.entrySet()) {
      this.quicHints.put(hostAndPort.getKey(), String.valueOf(hostAndPort.getValue()));
    }
    this.quicCanonicalSuffixes = quicCanonicalSuffixes;
    this.enableGzipDecompression = enableGzipDecompression;
    this.enableBrotliDecompression = enableBrotliDecompression;
    this.enableSocketTagging = enableSocketTagging;
    this.enableInterfaceBinding = enableInterfaceBinding;
    this.h2ConnectionKeepaliveIdleIntervalMilliseconds =
        h2ConnectionKeepaliveIdleIntervalMilliseconds;
    this.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds;
    this.maxConnectionsPerHost = maxConnectionsPerHost;
    this.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
    this.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds;
    this.appVersion = appVersion;
    this.appId = appId;
    this.trustChainVerification = trustChainVerification;
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

    this.runtimeGuards = new HashMap<String, String>();
    for (Map.Entry<String, Boolean> guardAndValue : runtimeGuards.entrySet()) {
      this.runtimeGuards.put(guardAndValue.getKey(), String.valueOf(guardAndValue.getValue()));
    }
    this.enablePlatformCertificatesValidation = enablePlatformCertificatesValidation;
    this.rtdsResourceName = rtdsResourceName;
    this.rtdsTimeoutSeconds = rtdsTimeoutSeconds;
    this.xdsAddress = xdsAddress;
    this.xdsPort = xdsPort;
    this.xdsGrpcInitialMetadata = new HashMap<>(xdsGrpcInitialMetadata);
    this.xdsRootCerts = xdsRootCerts;
    this.nodeId = nodeId;
    this.nodeRegion = nodeRegion;
    this.nodeZone = nodeZone;
    this.nodeSubZone = nodeSubZone;
    this.nodeMetadata = nodeMetadata;
    this.cdsResourcesLocator = cdsResourcesLocator;
    this.cdsTimeoutSeconds = cdsTimeoutSeconds;
    this.enableCds = enableCds;
  }

  public long createBootstrap() {
    Boolean enforceTrustChainVerification =
        trustChainVerification == EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;
    List<EnvoyNativeFilterConfig> reverseFilterChain = new ArrayList<>(nativeFilterChain);
    Collections.reverse(reverseFilterChain);

    byte[][] filterChain = JniBridgeUtility.toJniBytes(reverseFilterChain);
    byte[][] dnsPreresolve = JniBridgeUtility.stringsToJniBytes(dnsPreresolveHostnames);
    byte[][] runtimeGuards = JniBridgeUtility.mapToJniBytes(this.runtimeGuards);
    byte[][] quicHints = JniBridgeUtility.mapToJniBytes(this.quicHints);
    byte[][] quicSuffixes = JniBridgeUtility.stringsToJniBytes(quicCanonicalSuffixes);
    byte[][] xdsGrpcInitialMetadata = JniBridgeUtility.mapToJniBytes(this.xdsGrpcInitialMetadata);

    return JniLibrary.createBootstrap(
        connectTimeoutSeconds, dnsRefreshSeconds, dnsFailureRefreshSecondsBase,
        dnsFailureRefreshSecondsMax, dnsQueryTimeoutSeconds, dnsMinRefreshSeconds, dnsPreresolve,
        enableDNSCache, dnsCacheSaveIntervalSeconds, enableDrainPostDnsRefresh, enableHttp3,
        http3ConnectionOptions, http3ClientConnectionOptions, quicHints, quicSuffixes,
        enableGzipDecompression, enableBrotliDecompression, enableSocketTagging,
        enableInterfaceBinding, h2ConnectionKeepaliveIdleIntervalMilliseconds,
        h2ConnectionKeepaliveTimeoutSeconds, maxConnectionsPerHost, streamIdleTimeoutSeconds,
        perTryIdleTimeoutSeconds, appVersion, appId, enforceTrustChainVerification, filterChain,
        enablePlatformCertificatesValidation, runtimeGuards, rtdsResourceName, rtdsTimeoutSeconds,
        xdsAddress, xdsPort, xdsGrpcInitialMetadata, xdsRootCerts, nodeId, nodeRegion, nodeZone,
        nodeSubZone, nodeMetadata.toByteArray(), cdsResourcesLocator, cdsTimeoutSeconds, enableCds);
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException(String unresolvedKey) {
      super("Unresolved template key: " + unresolvedKey);
    }
  }
}
