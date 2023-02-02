package io.envoyproxy.envoymobile.engine;

/* Datatype used by the EnvoyConfiguration to create a native http filter chain. */
public class EnvoyNativeConfig {
  public final String name;
  public final String typedConfig;

  /**
   * Create a new instance of the configuration
   *
   * @param name        the name of the filter.
   * @param typedConfig the filter configuration.
   */
  public EnvoyNativeConfig(String name, String typedConfig) {
    this.name = name;
    this.typedConfig = typedConfig;
  }
}
