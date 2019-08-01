package io.envoyproxy.envoymobile.engine.types;

public class EnvoyData {
  public final long length;
  public final byte[] bytes;

  public EnvoyData(long length, byte[] bytes) {
    this.length = length;
    this.bytes = bytes;
  }
}
