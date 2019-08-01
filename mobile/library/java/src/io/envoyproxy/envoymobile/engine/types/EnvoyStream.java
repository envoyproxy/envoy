package io.envoyproxy.envoymobile.engine.types;

public class EnvoyStream {
  public final EnvoyStatus status;
  public final long stream;

  public EnvoyStream(EnvoyStatus status, long stream) {
    this.status = status;
    this.stream = stream;
  }
}
