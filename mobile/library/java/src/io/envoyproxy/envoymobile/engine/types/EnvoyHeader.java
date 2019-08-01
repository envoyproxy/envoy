package io.envoyproxy.envoymobile.engine.types;

public class EnvoyHeader {
  public final EnvoyData key;
  public final EnvoyData value;

  public EnvoyHeader(EnvoyData key, EnvoyData value) {
    this.key = key;
    this.value = value;
  }
}
