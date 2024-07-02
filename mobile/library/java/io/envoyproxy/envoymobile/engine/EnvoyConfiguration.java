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
    ACCEPT_UNTRUSTED
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
  public final Boolean useCares;
  public final Boolean useGro;
  public final String http3ConnectionOptions;
  public final String http3ClientConnectionOptions;
  public final Map<String, String> quicHints;
  public final List<String> quicCanonicalSuffixes;
  public final Boolean enableGzipDecompression;
  public final Boolean enableBrotliDecompression;
  public final Boolean enablePortMigration;
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
  public final String upstreamTlsSni;

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
   * @param useCares                                      whether to use the c_ares library for DNS
   * @param useGro                                        whether to use UDP GRO on upstream QUIC
   *     connections, if available.
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
   * @param enablePortMigration                           whether to enable quic port migration.
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
   * @param upstreamTlsSni                                the upstream TLS socket SNI override.
   */
  public EnvoyConfiguration(
      int connectTimeoutSeconds, int dnsRefreshSeconds, int dnsFailureRefreshSecondsBase,
      int dnsFailureRefreshSecondsMax, int dnsQueryTimeoutSeconds, int dnsMinRefreshSeconds,
      List<String> dnsPreresolveHostnames, boolean enableDNSCache, int dnsCacheSaveIntervalSeconds,
      boolean enableDrainPostDnsRefresh, boolean enableHttp3, boolean useCares, boolean useGro,
      String http3ConnectionOptions, String http3ClientConnectionOptions,
      Map<String, Integer> quicHints, List<String> quicCanonicalSuffixes,
      boolean enableGzipDecompression, boolean enableBrotliDecompression,
      boolean enablePortMigration, boolean enableSocketTagging, boolean enableInterfaceBinding,
      int h2ConnectionKeepaliveIdleIntervalMilliseconds, int h2ConnectionKeepaliveTimeoutSeconds,
      int maxConnectionsPerHost, int streamIdleTimeoutSeconds, int perTryIdleTimeoutSeconds,
      String appVersion, String appId, TrustChainVerification trustChainVerification,
      List<EnvoyNativeFilterConfig> nativeFilterChain,
      List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories,
      Map<String, EnvoyStringAccessor> stringAccessors,
      Map<String, EnvoyKeyValueStore> keyValueStores, Map<String, Boolean> runtimeGuards,
      boolean enablePlatformCertificatesValidation, String upstreamTlsSni) {
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
    this.useCares = useCares;
    this.useGro = useGro;
    this.http3ConnectionOptions = http3ConnectionOptions;
    this.http3ClientConnectionOptions = http3ClientConnectionOptions;
    this.quicHints = new HashMap<>();
    for (Map.Entry<String, Integer> hostAndPort : quicHints.entrySet()) {
      this.quicHints.put(hostAndPort.getKey(), String.valueOf(hostAndPort.getValue()));
    }
    this.quicCanonicalSuffixes = quicCanonicalSuffixes;
    this.enableGzipDecompression = enableGzipDecompression;
    this.enableBrotliDecompression = enableBrotliDecompression;
    this.enablePortMigration = enablePortMigration;
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
      String config = JniLibrary.getNativeFilterConfig(filterFactory.getFilterName());
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
    this.upstreamTlsSni = upstreamTlsSni;
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

    return JniLibrary.createBootstrap(
        connectTimeoutSeconds, dnsRefreshSeconds, dnsFailureRefreshSecondsBase,
        dnsFailureRefreshSecondsMax, dnsQueryTimeoutSeconds, dnsMinRefreshSeconds, dnsPreresolve,
        enableDNSCache, dnsCacheSaveIntervalSeconds, enableDrainPostDnsRefresh, enableHttp3,
        useCares, useGro, http3ConnectionOptions, http3ClientConnectionOptions, quicHints,
        quicSuffixes, enableGzipDecompression, enableBrotliDecompression, enablePortMigration,
        enableSocketTagging, enableInterfaceBinding, h2ConnectionKeepaliveIdleIntervalMilliseconds,
        h2ConnectionKeepaliveTimeoutSeconds, maxConnectionsPerHost, streamIdleTimeoutSeconds,
        perTryIdleTimeoutSeconds, appVersion, appId, enforceTrustChainVerification, filterChain,
        enablePlatformCertificatesValidation, upstreamTlsSni, runtimeGuards);
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException(String unresolvedKey) {
      super("Unresolved template key: " + unresolvedKey);
    }
  }
}
