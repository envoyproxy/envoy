package io.envoyproxy.envoymobile.engine.types;

/**
 * Exposes internal HTTP stream metrics, context, and other details.
 */
public interface EnvoyStreamIntel {
  /**
   * An internal identifier for the stream.
   */
  public long getStreamId();

  /**
   * An internal identifier for the connection carrying the stream.
   */
  public long getConnectionId();

  /**
   * The number of internal attempts to carry out a request/operation.
   */
  public long getAttemptCount();
}
