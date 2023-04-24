package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.HeaderMatchConfig;
import java.util.List;

/* Datatype used by the EnvoyConfiguration to configure virtual clusters.
 *
 * The fields map to their respective fields in
 * https://github.com/envoyproxy/envoy/blob/main/api/envoy/config/route/v3/route_components.proto
 * */
public class VirtualClusterConfig {
  // The name of the virtual cluster.
  public final String name;
  // The various rules to match requests to the virtual cluster.
  public final List<HeaderMatchConfig> matches;

  public VirtualClusterConfig(String name, List<HeaderMatchConfig> matches) {
    this.name = name;
    this.matches = matches;
  }
}
