package io.envoyproxy.envoymobile.engine;

import java.util.Collections;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.lang.StringBuilder;
import javax.annotation.Nullable;

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
  public final Boolean enableGzipCompression;
  public final Boolean enableBrotliDecompression;
  public final Boolean enableBrotliCompression;
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
  public final List<String> virtualClusters;
  public final List<EnvoyNativeFilterConfig> nativeFilterChain;
  public final Map<String, EnvoyStringAccessor> stringAccessors;
  public final Map<String, EnvoyKeyValueStore> keyValueStores;
  public final List<String> statSinks;
  public final Boolean enablePlatformCertificatesValidation;
  public final Boolean enableSkipDNSLookupForProxiedRequests;

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
   * @param enableGzipCompression                         whether to enable request gzip
   *     compression.
   * @param enableBrotliDecompression                     whether to enable response brotli
   *     decompression.
   * @param enableBrotliCompression                       whether to enable request brotli
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
   * @param virtualClusters                               the JSON list of virtual cluster configs.
   * @param nativeFilterChain                             the configuration for native filters.
   * @param httpPlatformFilterFactories                   the configuration for platform filters.
   * @param stringAccessors                               platform string accessors to register.
   * @param keyValueStores                                platform key-value store implementations.
   * @param enableSkipDNSLookupForProxiedRequests         whether to skip waiting on DNS response
   *     for proxied requests.
   */
  public EnvoyConfiguration(
      boolean adminInterfaceEnabled, String grpcStatsDomain, int connectTimeoutSeconds,
      int dnsRefreshSeconds, int dnsFailureRefreshSecondsBase, int dnsFailureRefreshSecondsMax,
      int dnsQueryTimeoutSeconds, int dnsMinRefreshSeconds, List<String> dnsPreresolveHostnames,
      boolean enableDNSCache, int dnsCacheSaveIntervalSeconds, boolean enableDrainPostDnsRefresh,
      boolean enableHttp3, boolean enableGzipDecompression, boolean enableGzipCompression,
      boolean enableBrotliDecompression, boolean enableBrotliCompression,
      boolean enableSocketTagging, boolean enableHappyEyeballs, boolean enableInterfaceBinding,
      int h2ConnectionKeepaliveIdleIntervalMilliseconds, int h2ConnectionKeepaliveTimeoutSeconds,
      int maxConnectionsPerHost, int statsFlushSeconds, int streamIdleTimeoutSeconds,
      int perTryIdleTimeoutSeconds, String appVersion, String appId,
      TrustChainVerification trustChainVerification, List<String> virtualClusters,
      List<EnvoyNativeFilterConfig> nativeFilterChain,
      List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories,
      Map<String, EnvoyStringAccessor> stringAccessors,
      Map<String, EnvoyKeyValueStore> keyValueStores, List<String> statSinks,
      Boolean enableSkipDNSLookupForProxiedRequests, boolean enablePlatformCertificatesValidation) {
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
    this.enableGzipCompression = enableGzipCompression;
    this.enableBrotliDecompression = enableBrotliDecompression;
    this.enableBrotliCompression = enableBrotliCompression;
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
    this.enablePlatformCertificatesValidation = enablePlatformCertificatesValidation;
    this.enableSkipDNSLookupForProxiedRequests = enableSkipDNSLookupForProxiedRequests;
  }
  /**
   * Creates configuration YAML based on the configuration of the class
   *
   * @return String, the resolved yaml.
   * @throws ConfigurationException, when the yaml provided is not fully
   *                                 resolved.
   */
  String createYaml() {
    final String configTemplate = JniLibrary.configTemplate();
    final String certValidationTemplate =
        JniLibrary.certValidationTemplate(enablePlatformCertificatesValidation);
    final String platformFilterTemplate = JniLibrary.platformFilterTemplate();
    final String nativeFilterTemplate = JniLibrary.nativeFilterTemplate();

    final StringBuilder customFiltersBuilder = new StringBuilder();

    for (EnvoyNativeFilterConfig filter : nativeFilterChain) {
      String filterConfig = nativeFilterTemplate.replace("{{ native_filter_name }}", filter.name)
                                .replace("{{ native_filter_typed_config }}", filter.typedConfig);
      customFiltersBuilder.append(filterConfig);
    }

    if (enableHttp3) {
      final String altProtocolCacheFilterInsert = JniLibrary.altProtocolCacheFilterInsert();
      customFiltersBuilder.append(altProtocolCacheFilterInsert);
    }

    if (enableGzipDecompression) {
      final String gzipFilterInsert = JniLibrary.gzipDecompressorConfigInsert();
      customFiltersBuilder.append(gzipFilterInsert);
    }

    if (enableGzipCompression) {
      final String gzipFilterInsert = JniLibrary.gzipCompressorConfigInsert();
      customFiltersBuilder.append(gzipFilterInsert);
    }

    if (enableBrotliDecompression) {
      final String brotliFilterInsert = JniLibrary.brotliDecompressorConfigInsert();
      customFiltersBuilder.append(brotliFilterInsert);
    }

    if (enableBrotliCompression) {
      final String brotliFilterInsert = JniLibrary.brotliCompressorConfigInsert();
      customFiltersBuilder.append(brotliFilterInsert);
    }

    if (enableSocketTagging) {
      final String socketTagFilterInsert = JniLibrary.socketTagConfigInsert();
      customFiltersBuilder.append(socketTagFilterInsert);
    }

    String processedTemplate =
        configTemplate.replace("#{custom_filters}", customFiltersBuilder.toString());
    String maybeComma = "";
    StringBuilder virtualClustersBuilder = new StringBuilder("[");
    for (String cluster : virtualClusters) {
      virtualClustersBuilder.append(maybeComma);
      virtualClustersBuilder.append(cluster);
      maybeComma = ",";
    }
    virtualClustersBuilder.append("]");

    maybeComma = "";
    StringBuilder dnsBuilder = new StringBuilder("[");
    for (String dns : dnsPreresolveHostnames) {
      dnsBuilder.append(maybeComma);
      dnsBuilder.append("{address: " + dns + ", port_value: 443}");
      maybeComma = ",";
    }
    dnsBuilder.append("]");

    StringBuilder configBuilder = new StringBuilder("!ignore platform_defs:\n");
    configBuilder.append(String.format("- &connect_timeout %ss\n", connectTimeoutSeconds))
        .append(String.format("- &dns_fail_base_interval %ss\n", dnsFailureRefreshSecondsBase))
        .append(String.format("- &dns_fail_max_interval %ss\n", dnsFailureRefreshSecondsMax))
        .append(String.format("- &dns_query_timeout %ss\n", dnsQueryTimeoutSeconds))
        .append(String.format("- &dns_min_refresh_rate %ss\n", dnsMinRefreshSeconds))
        .append(String.format("- &dns_preresolve_hostnames %s\n", dnsBuilder.toString()))
        .append(String.format("- &dns_lookup_family %s\n",
                              enableHappyEyeballs ? "ALL" : "V4_PREFERRED"))
        .append(String.format("- &dns_refresh_rate %ss\n", dnsRefreshSeconds))
        .append(String.format("- &enable_drain_post_dns_refresh %s\n",
                              enableDrainPostDnsRefresh ? "true" : "false"))
        .append(String.format("- &enable_interface_binding %s\n",
                              enableInterfaceBinding ? "true" : "false"))
        .append("- &force_ipv6 true\n")
        .append(String.format("- &h2_connection_keepalive_idle_interval %ss\n",
                              h2ConnectionKeepaliveIdleIntervalMilliseconds / 1000.0))
        .append(String.format("- &h2_connection_keepalive_timeout %ss\n",
                              h2ConnectionKeepaliveTimeoutSeconds))
        .append(String.format("- &max_connections_per_host %s\n", maxConnectionsPerHost))
        .append(String.format("- &stream_idle_timeout %ss\n", streamIdleTimeoutSeconds))
        .append(String.format("- &per_try_idle_timeout %ss\n", perTryIdleTimeoutSeconds))
        .append(String.format("- &metadata { device_os: %s, app_version: %s, app_id: %s }\n",
                              "Android", appVersion, appId))
        .append(String.format("- &trust_chain_verification %s\n", trustChainVerification.name()))
        .append(String.format("- &skip_dns_lookup_for_proxied_requests %s\n",
                              enableSkipDNSLookupForProxiedRequests ? "true" : "false"))
        .append("- &virtual_clusters ")
        .append(virtualClustersBuilder.toString())
        .append("\n");

    if (enableDNSCache) {
      configBuilder.append(
          String.format("- &persistent_dns_cache_save_interval %s\n", dnsCacheSaveIntervalSeconds));
      final String persistentDNSCacheConfigInsert = JniLibrary.persistentDNSCacheConfigInsert();
      configBuilder.append(
          String.format("- &persistent_dns_cache_config %s\n", persistentDNSCacheConfigInsert));
    }

    configBuilder.append(String.format("- &stats_flush_interval %ss\n", statsFlushSeconds));

    List<String> stat_sinks_config = new ArrayList<>(statSinks);
    if (grpcStatsDomain != null) {
      if (!grpcStatsDomain.isEmpty()) {
        stat_sinks_config.add("*base_metrics_service");
        configBuilder.append("- &stats_domain ").append(grpcStatsDomain).append("\n");
      } else {
        configBuilder.append("- &stats_domain ").append("127.0.0.1").append("\n");
      }
    }

    if (!stat_sinks_config.isEmpty()) {
      configBuilder.append("- &stats_sinks [");
      configBuilder.append(stat_sinks_config.get(0));
      for (int i = 1; i < stat_sinks_config.size(); i++) {
        configBuilder.append(',').append(stat_sinks_config.get(i));
      }
      configBuilder.append("] \n");
    }

    // Add a new anchor to override the default anchors in config header.
    configBuilder.append(certValidationTemplate).append("\n");

    if (adminInterfaceEnabled) {
      configBuilder.append("admin: *admin_interface\n");
    }

    configBuilder.append(processedTemplate);
    String resolvedConfiguration = configBuilder.toString();

    final Matcher unresolvedKeys = UNRESOLVED_KEY_PATTERN.matcher(resolvedConfiguration);
    if (unresolvedKeys.find()) {
      throw new ConfigurationException(unresolvedKeys.group(1));
    }

    for (StackTraceElement element : Thread.currentThread().getStackTrace()) {
      if (element.getClassName().startsWith("org.junit.")) {
        List<EnvoyNativeFilterConfig> reverseFilterChain = new ArrayList<>(nativeFilterChain);
        Collections.reverse(reverseFilterChain);
        Boolean enforceTrustChainVerification =
            trustChainVerification == EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;

        byte[][] filter_chain = JniBridgeUtility.toJniBytes(reverseFilterChain);
        byte[][] clusters = JniBridgeUtility.stringsToJniBytes(virtualClusters);
        byte[][] stats_sinks = JniBridgeUtility.stringsToJniBytes(statSinks);
        byte[][] dns_preresolve = JniBridgeUtility.stringsToJniBytes(dnsPreresolveHostnames);

        JniLibrary.compareYaml(
            resolvedConfiguration, grpcStatsDomain, adminInterfaceEnabled, connectTimeoutSeconds,
            dnsRefreshSeconds, dnsFailureRefreshSecondsBase, dnsFailureRefreshSecondsMax,
            dnsQueryTimeoutSeconds, dnsMinRefreshSeconds, dns_preresolve, enableDNSCache,
            dnsCacheSaveIntervalSeconds, enableDrainPostDnsRefresh, enableHttp3,
            enableGzipDecompression, enableGzipCompression, enableBrotliDecompression,
            enableBrotliCompression, enableSocketTagging, enableHappyEyeballs,
            enableInterfaceBinding, h2ConnectionKeepaliveIdleIntervalMilliseconds,
            h2ConnectionKeepaliveTimeoutSeconds, maxConnectionsPerHost, statsFlushSeconds,
            streamIdleTimeoutSeconds, perTryIdleTimeoutSeconds, appVersion, appId,
            enforceTrustChainVerification, clusters, filter_chain, stats_sinks,
            enablePlatformCertificatesValidation, enableSkipDNSLookupForProxiedRequests);
        break;
      }
    }

    return resolvedConfiguration;
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException(String unresolvedKey) {
      super("Unresolved template key: " + unresolvedKey);
    }
  }
}
