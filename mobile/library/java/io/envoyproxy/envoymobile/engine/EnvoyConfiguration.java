package io.envoyproxy.envoymobile.engine;

import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;
import java.util.regex.Matcher;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;

/* Typed configuration that may be used for starting Envoy. */
public class EnvoyConfiguration {
  public final String statsDomain;
  public final Integer connectTimeoutSeconds;
  public final Integer dnsRefreshSeconds;
  public final Integer dnsFailureRefreshSecondsBase;
  public final Integer dnsFailureRefreshSecondsMax;
  public final List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories;
  public final Integer statsFlushSeconds;
  public final String appVersion;
  public final String appId;
  public final String virtualClusters;
  public final List<EnvoyNativeFilterConfig> nativeFilterChain;
  public final Map<String, EnvoyStringAccessor> stringAccessors;

  private static final Pattern UNRESOLVED_KEY_PATTERN = Pattern.compile("\\{\\{ (.+) \\}\\}");

  /**
   * Create a new instance of the configuration.
   *
   * @param statsDomain                  the domain to flush stats to.
   * @param connectTimeoutSeconds        timeout for new network connections to hosts in
   *                                     the cluster.
   * @param dnsRefreshSeconds            rate in seconds to refresh DNS.
   * @param dnsFailureRefreshSecondsBase base rate in seconds to refresh DNS on failure.
   * @param dnsFailureRefreshSecondsMax  max rate in seconds to refresh DNS on failure.
   * @param statsFlushSeconds            interval at which to flush Envoy stats.
   * @param appVersion                   the App Version of the App using this Envoy Client.
   * @param appId                        the App ID of the App using this Envoy Client.
   * @param virtualClusters              the JSON list of virtual cluster configs.
   * @param nativeFilterChain            the configuration for native filters.
   * @param httpPlatformFilterFactories          the configuration for platform filters.
   * @param stringAccesssors             platform string accessors to register.
   */
  public EnvoyConfiguration(String statsDomain, int connectTimeoutSeconds, int dnsRefreshSeconds,
                            int dnsFailureRefreshSecondsBase, int dnsFailureRefreshSecondsMax,
                            int statsFlushSeconds, String appVersion, String appId,
                            String virtualClusters, List<EnvoyNativeFilterConfig> nativeFilterChain,
                            List<EnvoyHTTPFilterFactory> httpPlatformFilterFactories,
                            Map<String, EnvoyStringAccessor> stringAccessors) {
    this.statsDomain = statsDomain;
    this.connectTimeoutSeconds = connectTimeoutSeconds;
    this.dnsRefreshSeconds = dnsRefreshSeconds;
    this.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
    this.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
    this.statsFlushSeconds = statsFlushSeconds;
    this.appVersion = appVersion;
    this.appId = appId;
    this.virtualClusters = virtualClusters;
    this.nativeFilterChain = nativeFilterChain;
    this.httpPlatformFilterFactories = httpPlatformFilterFactories;
    this.stringAccessors = stringAccessors;
  }

  /**
   * Resolves the provided configuration template using properties on this
   * configuration.
   *
   * @param templateYAML the template configuration to resolve.
   * @param statsSinkTemplateYAML helper template to add the stats sink.
   * @param platformFilterTemplateYAML helper template to build platform http filters.
   * @param nativeFilterTemplateYAML helper template to build native http filters.
   * @return String, the resolved template.
   * @throws ConfigurationException, when the template provided is not fully
   *                                 resolved.
   */
  String resolveTemplate(final String templateYAML, final String statsSinkTemplateYAML,
                         final String platformFilterTemplateYAML,
                         final String nativeFilterTemplateYAML) {
    final StringBuilder filterConfigBuilder = new StringBuilder();
    for (EnvoyHTTPFilterFactory filterFactory : httpPlatformFilterFactories) {
      String filterConfig = platformFilterTemplateYAML.replace("{{ platform_filter_name }}",
                                                               filterFactory.getFilterName());
      filterConfigBuilder.append(filterConfig);
    }
    String filterConfigChain = filterConfigBuilder.toString();

    final StringBuilder nativeFilterConfigBuilder = new StringBuilder();
    for (EnvoyNativeFilterConfig filter : nativeFilterChain) {
      String nativeFilterConfig =
          nativeFilterTemplateYAML.replace("{{ native_filter_name }}", filter.name)
              .replace("{{ native_filter_typed_config }}", filter.typedConfig);
      nativeFilterConfigBuilder.append(nativeFilterConfig);
    }
    String nativeFilterConfigChain = nativeFilterConfigBuilder.toString();

    String resolvedConfiguration =
        templateYAML.replace("{{ stats_domain }}", statsDomain != null ? statsDomain : "0.0.0.0")
            .replace("{{ stats_sink }}", statsDomain != null ? statsSinkTemplateYAML : "")
            .replace("{{ platform_filter_chain }}", filterConfigChain)
            .replace("{{ connect_timeout_seconds }}", String.format("%s", connectTimeoutSeconds))
            .replace("{{ dns_refresh_rate_seconds }}", String.format("%s", dnsRefreshSeconds))
            .replace("{{ dns_failure_refresh_rate_seconds_base }}",
                     String.format("%s", dnsFailureRefreshSecondsBase))
            .replace("{{ dns_failure_refresh_rate_seconds_max }}",
                     String.format("%s", dnsFailureRefreshSecondsMax))
            .replace("{{ stats_flush_interval_seconds }}", String.format("%s", statsFlushSeconds))
            .replace("{{ device_os }}", "Android")
            .replace("{{ app_version }}", appVersion)
            .replace("{{ app_id }}", appId)
            .replace("{{ virtual_clusters }}", virtualClusters)
            // TODO(@buildbreaker): Update these empty values to expose direct responses:
            // https://github.com/envoyproxy/envoy-mobile/issues/1291
            .replace("{{ fake_cluster_matchers }}", "")
            .replace("{{ fake_remote_cluster }}", "")
            .replace("{{ fake_remote_listener }}", "")
            .replace("{{ route_reset_filter }}", "")
            .replace("{{ native_filter_chain }}", nativeFilterConfigChain);

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
