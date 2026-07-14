package io.envoyproxy.envoymobile.engine.types;

// This is a superset of the connection types in the NetInfo v3 specification:
// http://w3c.github.io/netinfo/.
// This should be in sync with ConnectionType in network_types.h
public enum EnvoyConnectionType {
  CONNECTION_UNKNOWN(0), // A connection exists, but its type is unknown.
                         // Also used as a default value.
  CONNECTION_BLUETOOTH(1),
  CONNECTION_ETHERNET(2),
  CONNECTION_WIFI(3),
  CONNECTION_2G(4),
  CONNECTION_3G(5),
  CONNECTION_4G(6),
  CONNECTION_5G(7),
  CONNECTION_NONE(8); // No connection.

  private final int value;

  private EnvoyConnectionType(int value) { this.value = value; }

  public int getValue() { return value; }
}
