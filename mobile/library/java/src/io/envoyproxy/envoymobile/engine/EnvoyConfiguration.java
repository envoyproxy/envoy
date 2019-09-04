package io.envoyproxy.envoymobile.engine;

public class EnvoyConfiguration {

  public final Integer connectTimeoutSeconds;
  public final Integer dnsRefreshSeconds;
  public final Integer statsFlushSeconds;

  /**
   * Create an EnvoyConfiguration with a user provided configuration values.
   *
   * @param connectTimeoutSeconds timeout for new network connections to hosts in the cluster.
   * @param dnsRefreshSeconds rate in seconds to refresh DNS.
   * @param statsFlushSeconds interval at which to flush Envoy stats.
   */
  public EnvoyConfiguration(int connectTimeoutSeconds, int dnsRefreshSeconds,
                            int statsFlushSeconds) {
    this.connectTimeoutSeconds = connectTimeoutSeconds;
    this.dnsRefreshSeconds = dnsRefreshSeconds;
    this.statsFlushSeconds = statsFlushSeconds;
  }

  /**
   * Resolves the provided configuration template using properties on this configuration.
   * This default configuration is provided by the native layer.
   *
   * @param templateYAML the default template configuration.
   * @return String, the resolved template.
   * @throws ConfigurationException, when the template provided is not fully resolved.
   */
  String resolveTemplate(String templateYAML) {
    String resolvedConfiguration =
        templateYAML.replace("{{ connect_timeout }}", String.format("%ss", connectTimeoutSeconds))
            .replace("{{ dns_refresh_rate }}", String.format("%ss", dnsRefreshSeconds))
            .replace("{{ stats_flush_interval }}", String.format("%ss", statsFlushSeconds));

    if (resolvedConfiguration.contains("{{")) {
      throw new ConfigurationException();
    }
    return resolvedConfiguration;
  }

  static class ConfigurationException extends RuntimeException {
    ConfigurationException() { super("Unresolved Template Key"); }
  }
}
