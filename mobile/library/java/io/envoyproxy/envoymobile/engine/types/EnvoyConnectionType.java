package io.envoyproxy.envoymobile.engine.types;

// This is a superset of the connection types in the NetInfo v3 specification:
// http://w3c.github.io/netinfo/.
public enum EnvoyConnectionType {
  CONNECTION_UNKNOWN(0), // A connection exists, but its type is unknown.
                         // Also used as a default value.
  CONNECTION_ETHERNET(1),
  CONNECTION_WIFI(2),
  CONNECTION_2G(3),
  CONNECTION_3G(4),
  CONNECTION_4G(5),
  CONNECTION_NONE(6), // No connection.
  CONNECTION_BLUETOOTH(7),
  CONNECTION_5G(8),
  CONNECTION_LAST(8);

  private final int value;

  private EnvoyConnectionType(int value) { this.value = value; }

  public int getValue() { return value; }
}
