package io.envoyproxy.envoymobile.engine;

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
  public final String dnsPreresolveHostnames;
  public final List<String> dnsFallbackNameservers;
  public final Boolean dnsFilterUnroutableFamilies;
  public final Boolean dnsUseSystemResolver;
  public final Boolean enableDrainPostDnsRefresh;
  public final Boolean enableHttp3;
  public final Boolean enableGzip;
  public final Boolean enableBrotli;
  public final Boolean enableSocketTagging;
  public final Boolean enableHappyEyeballs;
  public final Boolean enableInterfaceBinding;
  public final Integer h2ConnectionKeepaliveIdleIntervalMilliseconds;
  public final Integer h2ConnectionKeepaliveTimeoutSeconds;
  public final Boolean h2ExtendKeepaliveTimeout;
  public final Integer maxConnectionsPerHost;
  public final List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories;
  public final Integer statsFlushSeconds;
  public final Integer streamIdleTimeoutSeconds;
  public final Integer perTryIdleTimeoutSeconds;
  public final String appVersion;
  public final String appId;
  public final TrustChainVerification trustChainVerification;
  public final String virtualClusters;
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
   * @param dnsFallbackNameservers                        addresses to use as DNS name server
   *     fallback.
   * @param dnsFilterUnroutableFamilies                   whether to filter unroutable IP families
   *     or not.
   * @param dnsUseSystemResolver                          whether to use the getaddrinfo-based
   *     system resolver or
   *                                                      c-ares.
   * @param enableDrainPostDnsRefresh                     whether to drain connections after soft
   *     DNS refresh.
   * @param enableHttp3                                   whether to enable experimental support for
   *     HTTP/3 (QUIC).
   * @param enableGzip                                    whether to enable response gzip
   *     decompression.
   * @param enableBrotli                                  whether to enable response brotli
   *     decompression.
   * @param enableSocketTagging                           whether to enable socket tagging.
   * @param enableHappyEyeballs                           whether to enable RFC 6555 handling for
   *     IPv4/IPv6.
   * @param enableInterfaceBinding                        whether to allow interface binding.
   * @param h2ConnectionKeepaliveIdleIntervalMilliseconds rate in milliseconds seconds to send h2
   *                                                      pings on stream creation.
   * @param h2ConnectionKeepaliveTimeoutSeconds           rate in seconds to timeout h2 pings.
   * @param h2ExtendKeepaliveTimeout                      Extend the keepalive timeout when *any*
   *                                                      frame is received
   *                                                      on the owning HTTP/2 connection.
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
      int dnsQueryTimeoutSeconds, int dnsMinRefreshSeconds, String dnsPreresolveHostnames,
      List<String> dnsFallbackNameservers, boolean dnsFilterUnroutableFamilies,
      boolean dnsUseSystemResolver, boolean enableDrainPostDnsRefresh, boolean enableHttp3,
      boolean enableGzip, boolean enableBrotli, boolean enableSocketTagging,
      boolean enableHappyEyeballs, boolean enableInterfaceBinding,
      int h2ConnectionKeepaliveIdleIntervalMilliseconds, int h2ConnectionKeepaliveTimeoutSeconds,
      boolean h2ExtendKeepaliveTimeout, int maxConnectionsPerHost, int statsFlushSeconds,
      int streamIdleTimeoutSeconds, int perTryIdleTimeoutSeconds, String appVersion, String appId,
      TrustChainVerification trustChainVerification, String virtualClusters,
      List<EnvoyNativeFilterConfig> nativeFilterChain,
      List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories,
      Map<String, EnvoyStringAccessor> stringAccessors,
      Map<String, EnvoyKeyValueStore> keyValueStores, List<String> statSinks,
      Boolean enableSkipDNSLookupForProxiedRequests, boolean enablePlatformCertificatesValidation) {
    this.adminInterfaceEnabled = adminInterfaceEnabled;
    this.grpcStatsDomain = grpcStatsDomain;
    this.connectTimeoutSeconds = connectTimeoutSeconds;
    this.dnsRefreshSeconds = dnsRefreshSeconds;
    this.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
    this.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
    this.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds;
    this.dnsMinRefreshSeconds = dnsMinRefreshSeconds;
    this.dnsPreresolveHostnames = dnsPreresolveHostnames;
    this.dnsFallbackNameservers = dnsFallbackNameservers;
    this.dnsFilterUnroutableFamilies = dnsFilterUnroutableFamilies;
    this.dnsUseSystemResolver = dnsUseSystemResolver;
    this.enableDrainPostDnsRefresh = enableDrainPostDnsRefresh;
    this.enableHttp3 = enableHttp3;
    this.enableGzip = enableGzip;
    this.enableBrotli = enableBrotli;
    this.enableSocketTagging = enableSocketTagging;
    this.enableHappyEyeballs = enableHappyEyeballs;
    this.enableInterfaceBinding = enableInterfaceBinding;
    this.h2ConnectionKeepaliveIdleIntervalMilliseconds =
        h2ConnectionKeepaliveIdleIntervalMilliseconds;
    this.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds;
    this.h2ExtendKeepaliveTimeout = h2ExtendKeepaliveTimeout;
    this.maxConnectionsPerHost = maxConnectionsPerHost;
    this.statsFlushSeconds = statsFlushSeconds;
    this.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
    this.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds;
    this.appVersion = appVersion;
    this.appId = appId;
    this.trustChainVerification = trustChainVerification;
    this.virtualClusters = virtualClusters;
    this.nativeFilterChain = nativeFilterChain;
    this.httpPlatformFilterFactories = httpPlatformFilterFactories;
    this.stringAccessors = stringAccessors;
    this.keyValueStores = keyValueStores;
    this.statSinks = statSinks;
    this.enablePlatformCertificatesValidation = enablePlatformCertificatesValidation;
    this.enableSkipDNSLookupForProxiedRequests = enableSkipDNSLookupForProxiedRequests;
  }

  /**
   * Resolves the provided configuration template using properties on this
   * configuration.
   *
   * @param configTemplate the template configuration to resolve.
   * @param platformFilterTemplate helper template to build platform http filters.
   * @param nativeFilterTemplate helper template to build native http filters.
   * @param altProtocolCacheFilterInsert helper insert to include the alt protocol cache filter.
   * @return String, the resolved template.
   * @throws ConfigurationException, when the template provided is not fully
   *                                 resolved.
   */
  String resolveTemplate(final String configTemplate, final String platformFilterTemplate,
                         final String nativeFilterTemplate,
                         final String altProtocolCacheFilterInsert, final String gzipFilterInsert,
                         final String brotliFilterInsert, final String socketTagFilterInsert,
                         final String certValidationTemplate) {
    final StringBuilder customFiltersBuilder = new StringBuilder();

    for (EnvoyHTTPFilterFactory filterFactory : httpPlatformFilterFactories) {
      String filterConfig = platformFilterTemplate.replace("{{ platform_filter_name }}",
                                                           filterFactory.getFilterName());
      customFiltersBuilder.append(filterConfig);
    }

    for (EnvoyNativeFilterConfig filter : nativeFilterChain) {
      String filterConfig = nativeFilterTemplate.replace("{{ native_filter_name }}", filter.name)
                                .replace("{{ native_filter_typed_config }}", filter.typedConfig);
      customFiltersBuilder.append(filterConfig);
    }

    if (enableHttp3) {
      customFiltersBuilder.append(altProtocolCacheFilterInsert);
    }

    if (enableGzip) {
      customFiltersBuilder.append(gzipFilterInsert);
    }

    if (enableBrotli) {
      customFiltersBuilder.append(brotliFilterInsert);
    }
    if (enableSocketTagging) {
      customFiltersBuilder.append(socketTagFilterInsert);
    }

    String processedTemplate =
        configTemplate.replace("#{custom_filters}", customFiltersBuilder.toString());

    String dnsFallbackNameserversAsString = "[]";
    if (!dnsFallbackNameservers.isEmpty()) {
      StringBuilder sb = new StringBuilder("[");
      String separator = "";
      for (String nameserver : dnsFallbackNameservers) {
        sb.append(separator);
        separator = ",";
        sb.append(String.format("{\"socket_address\":{\"address\":\"%s\"}}", nameserver));
      }
      sb.append("]");
      dnsFallbackNameserversAsString = sb.toString();
    }

    String dnsResolverName = "";
    String dnsResolverConfig = "";
    if (dnsUseSystemResolver) {
      dnsResolverName = "envoy.network.dns_resolver.getaddrinfo";
      dnsResolverConfig =
          "{\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig\"}";
    } else {
      dnsResolverName = "envoy.network.dns_resolver.cares";
      dnsResolverConfig = String.format(
          "{\"@type\":\"type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig\",\"resolvers\":%s,\"use_resolvers_as_fallback\": %s, \"filter_unroutable_families\": %s}",
          dnsFallbackNameserversAsString, !dnsFallbackNameservers.isEmpty() ? "true" : "false",
          dnsFilterUnroutableFamilies ? "true" : "false");
    }

    StringBuilder configBuilder = new StringBuilder("!ignore platform_defs:\n");
    configBuilder.append(String.format("- &connect_timeout %ss\n", connectTimeoutSeconds))
        .append(String.format("- &dns_fail_base_interval %ss\n", dnsFailureRefreshSecondsBase))
        .append(String.format("- &dns_fail_max_interval %ss\n", dnsFailureRefreshSecondsMax))
        .append(String.format("- &dns_query_timeout %ss\n", dnsQueryTimeoutSeconds))
        .append(String.format("- &dns_min_refresh_rate %ss\n", dnsMinRefreshSeconds))
        .append(String.format("- &dns_preresolve_hostnames %s\n", dnsPreresolveHostnames))
        .append(String.format("- &dns_lookup_family %s\n",
                              enableHappyEyeballs ? "ALL" : "V4_PREFERRED"))
        .append(
            String.format("- &dns_multiple_addresses %s\n", enableHappyEyeballs ? "true" : "false"))
        .append(String.format("- &h2_delay_keepalive_timeout %s\n",
                              h2ExtendKeepaliveTimeout ? "true" : "false"))
        .append(String.format("- &dns_resolver_name %s\n", dnsResolverName))
        .append(String.format("- &dns_refresh_rate %ss\n", dnsRefreshSeconds))
        .append(String.format("- &dns_resolver_config %s\n", dnsResolverConfig))
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
        .append(virtualClusters)
        .append("\n");

    configBuilder.append(String.format("- &stats_flush_interval %ss\n", statsFlushSeconds));

    List<String> stat_sinks_config = new ArrayList<>(statSinks);
    if (grpcStatsDomain != null) {
      stat_sinks_config.add("*base_metrics_service");
      configBuilder.append("- &stats_domain ").append(grpcStatsDomain).append("\n");
    }

    if (!stat_sinks_config.isEmpty()) {
      configBuilder.append("- &stats_sinks [");
      configBuilder.append(String.join(",", stat_sinks_config));
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
    return resolvedConfiguration;
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException(String unresolvedKey) {
      super("Unresolved template key: " + unresolvedKey);
    }
  }
}
