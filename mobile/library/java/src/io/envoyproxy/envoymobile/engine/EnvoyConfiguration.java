package io.envoyproxy.envoymobile.engine;

public interface EnvoyConfiguration {

  /**
   * Provides a default configuration template that may be used for starting Envoy.
   *
   * @return A template that may be used as a starting point for constructing configurations.
   */
  String templateString();
}
