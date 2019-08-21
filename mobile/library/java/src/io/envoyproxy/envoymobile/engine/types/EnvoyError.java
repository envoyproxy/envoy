package io.envoyproxy.envoymobile.engine.types;

public class EnvoyError {
  public final EnvoyErrorCode errorCode;
  public final String message;

  public EnvoyError(EnvoyErrorCode errorCode, String message) {
    this.errorCode = errorCode;
    this.message = message;
  }
}
