package io.envoyproxy.envoymobile.engine;

import java.util.List;

/* Datatype used by the EnvoyConfiguration to header matches for virtual clusters
 *
 * All the fields here map to the fields by the respective names in the HeaderMatcher message
 * defined here:
 * https://github.com/envoyproxy/envoy/blob/main/api/envoy/config/route/v3/route_components.proto
 * */
public class HeaderMatchConfig {
  // Indicates the match value is for an exact match.
  public static final int EXACT = 0;
  // Indicates the match value is a regular expression.
  public static final int SAFE_REGEX = 1;

  // The name of the matcher.
  public final String name;
  // The type of the matcher, one of the types defined above.
  public final int type;
  // The value of the matcher, interpreted based on the defined type.
  public final String value;

  public HeaderMatchConfig(String name, int type, String value) {
    this.name = name;
    this.type = type;
    this.value = value;
  }
}
