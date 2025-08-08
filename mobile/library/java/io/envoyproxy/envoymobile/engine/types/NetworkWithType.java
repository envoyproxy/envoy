package io.envoyproxy.envoymobile.engine.types;

public class NetworkWithType {
  // an opaque handle to a network interface.
  private final long netId;
  private final EnvoyConnectionType connectionType;

  public NetworkWithType(long netId, EnvoyConnectionType connectionType) {
    this.netId = netId;
    this.connectionType = connectionType;
  }

  public long getNetId() { return netId; }

  public EnvoyConnectionType getConnectionType() { return connectionType; }
}
