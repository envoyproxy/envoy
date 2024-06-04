package io.envoyproxy.envoymobile.engine.types;

// Network interface type
public enum EnvoyNetworkType {
  GENERIC(0),
  WLAN(1),
  WWAN(2),
  ;

  private final int value;

  EnvoyNetworkType(int value) { this.value = value; }

  /** Gets the numerical value of this enum. */
  public int getValue() { return value; }
}
