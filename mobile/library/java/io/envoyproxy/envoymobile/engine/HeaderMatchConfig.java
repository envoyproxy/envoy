package io.envoyproxy.envoymobile.engine;

import java.util.List;

/* Datatype used by the EnvoyConfiguration to header matches for virtual clusters */
public class HeaderMatchConfig {
  public static final int EXACT = 0;
  public static final int SAFE_REGEX = 1;

  public final String name;
  public final int type;
  public final String value;

  public HeaderMatchConfig(String name, int type, String value) {
    this.name = name;
    this.type = type;
    this.value = value;
  }
}
