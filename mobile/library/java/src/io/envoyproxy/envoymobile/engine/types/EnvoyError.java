package io.envoyproxy.envoymobile.engine.types;

public class EnvoyError {
  public final EnvoyErrorCode errorCode;
  public final EnvoyData message;

  public EnvoyError(EnvoyErrorCode errorCode, EnvoyData message) {
    this.errorCode = errorCode;
    this.message = message;
  }
}
