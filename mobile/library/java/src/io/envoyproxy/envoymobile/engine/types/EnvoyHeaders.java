package io.envoyproxy.envoymobile.engine.types;

public class EnvoyHeaders {
  public final long length;
  public final EnvoyHeader[] envoyHeaders;

  public EnvoyHeaders(long length, EnvoyHeader[] envoyHeaders) {
    this.length = length;
    this.envoyHeaders = envoyHeaders;
  }
}
