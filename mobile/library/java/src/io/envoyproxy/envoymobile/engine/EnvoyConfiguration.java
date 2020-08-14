package io.envoyproxy.envoymobile.engine;

import java.util.List;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;

/* Typed configuration that may be used for starting Envoy. */
public class EnvoyConfiguration {
  public final String statsDomain;
  public final Integer connectTimeoutSeconds;
  public final Integer dnsRefreshSeconds;
  public final Integer dnsFailureRefreshSecondsBase;
  public final Integer dnsFailureRefreshSecondsMax;
  public final List<EnvoyHTTPFilterFactory> httpFilterFactories;
  public final Integer statsFlushSeconds;
  public final String appVersion;
  public final String appId;
  public final String virtualClusters;

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
   */
  public EnvoyConfiguration(String statsDomain, int connectTimeoutSeconds, int dnsRefreshSeconds,
                            int dnsFailureRefreshSecondsBase, int dnsFailureRefreshSecondsMax,
                            List<EnvoyHTTPFilterFactory> httpFilterFactories, int statsFlushSeconds,
                            String appVersion, String appId, String virtualClusters) {
    this.statsDomain = statsDomain;
    this.connectTimeoutSeconds = connectTimeoutSeconds;
    this.dnsRefreshSeconds = dnsRefreshSeconds;
    this.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
    this.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
    this.httpFilterFactories = httpFilterFactories;
    this.statsFlushSeconds = statsFlushSeconds;
    this.appVersion = appVersion;
    this.appId = appId;
    this.virtualClusters = virtualClusters;
  }

  /**
   * Resolves the provided configuration template using properties on this
   * configuration.
   *
   * @param templateYAML the template configuration to resolve.
   * @return String, the resolved template.
   * @throws ConfigurationException, when the template provided is not fully
   *                                 resolved.
   */
  String resolveTemplate(final String templateYAML, final String filterTemplateYAML) {
    final StringBuilder filterConfigBuilder = new StringBuilder();
    for (EnvoyHTTPFilterFactory filterFactory : httpFilterFactories) {
      String filterConfig =
          filterTemplateYAML.replace("{{ platform_filter_name }}", filterFactory.getFilterName());
      filterConfigBuilder.append(filterConfig);
    }
    String filterConfigChain = filterConfigBuilder.toString();

    String resolvedConfiguration =
        templateYAML.replace("{{ stats_domain }}", statsDomain)
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
            .replace("{{ virtual_clusters }}", virtualClusters);

    if (resolvedConfiguration.contains("{{")) {
      throw new ConfigurationException();
    }
    return resolvedConfiguration;
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException() { super("Unresolved Template Key"); }
  }
}
