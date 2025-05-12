package io.envoyproxy.envoymobile.engine;

/* Datatype used by the EnvoyConfiguration to header matches for virtual clusters
 *
 * All the fields here map to the fields by the respective names in the HeaderMatcher message
 * defined here:
 * https://github.com/envoyproxy/envoy/blob/main/api/envoy/config/route/v3/route_components.proto
 * */
public class HeaderMatchConfig {
  public enum Type {
    // Indicates the match value is for an exact match.
    EXACT,
    // Indicates the match value is a regular expression.
    SAFE_REGEX
  }

  // The name of the matcher.
  public final String name;
  // The type of the matcher.
  public final Type type;
  // The value of the matcher, interpreted based on the defined type.
  public final String value;

  public HeaderMatchConfig(String name, Type type, String value) {
    this.name = name;
    this.type = type;
    this.value = value;
  }
}
