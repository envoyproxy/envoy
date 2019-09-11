package io.envoyproxy.envoymobile.engine.types;

public enum EnvoyErrorCode {
  ENVOY_UNDEFINED_ERROR,
  ENVOY_STREAM_RESET;

  public static EnvoyErrorCode fromInt(int val) {
    switch (val) {
    case 0:
      return ENVOY_UNDEFINED_ERROR;
    case 1:
      return ENVOY_STREAM_RESET;
    default:
      return ENVOY_UNDEFINED_ERROR;
    }
  }
}
