package io.envoyproxy.envoymobile.engine;

public class EnvoyConfiguration {

  public final String domain;
  public final Integer connectTimeoutSeconds;
  public final Integer dnsRefreshSeconds;
  public final Integer statsFlushSeconds;

  /**
   * Create an EnvoyConfiguration with a user provided configuration values.
   *
   * @param domain                The domain to use with Envoy (i.e.,
   *                              `api.foo.com`). TODO:
   *                              https://github.com/lyft/envoy-mobile/issues/433
   * @param connectTimeoutSeconds timeout for new network connections to hosts in
   *                              the cluster.
   * @param dnsRefreshSeconds     rate in seconds to refresh DNS.
   * @param statsFlushSeconds     interval at which to flush Envoy stats.
   */
  public EnvoyConfiguration(String domain, int connectTimeoutSeconds, int dnsRefreshSeconds,
                            int statsFlushSeconds) {
    this.domain = domain;
    this.connectTimeoutSeconds = connectTimeoutSeconds;
    this.dnsRefreshSeconds = dnsRefreshSeconds;
    this.statsFlushSeconds = statsFlushSeconds;
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
  String resolveTemplate(String templateYAML) {
    String resolvedConfiguration =
        templateYAML.replace("{{ domain }}", domain)
            .replace("{{ connect_timeout_seconds }}", String.format("%s", connectTimeoutSeconds))
            .replace("{{ dns_refresh_rate_seconds }}", String.format("%s", dnsRefreshSeconds))
            .replace("{{ stats_flush_interval_seconds }}", String.format("%s", statsFlushSeconds));

    if (resolvedConfiguration.contains("{{")) {
      throw new ConfigurationException();
    }
    return resolvedConfiguration;
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException() { super("Unresolved Template Key"); }
  }
}
