package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.HeaderMatchConfig;
import java.util.List;

/* Datatype used by the EnvoyConfiguration to configure virtual clusters. */
public class VirtualClusterConfig {
  public final String name;
  public final List<HeaderMatchConfig> matches;

  public VirtualClusterConfig(String name, List<HeaderMatchConfig> matches) {
    this.name = name;
    this.matches = matches;
  }
}
