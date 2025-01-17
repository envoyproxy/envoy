package io.envoyproxy.envoymobile.engine.types;

// Network interface type
public enum EnvoyNetworkType {
  GENERIC(1),
  WLAN(2),
  WWAN(4),
  WWAN_2G(8),
  WWAN_3G(16),
  WWAN_4G(32),
  WWAN_5G(64),
  ;

  private final int value;

  EnvoyNetworkType(int value) { this.value = value; }

  /** Gets the numerical value of this enum. */
  public int getValue() { return value; }
}
