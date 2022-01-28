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

  /**
   * The number of bytes consumed by the non terminal callbacks, from the response.
   *
   * <p>>NOTE: on terminal callbacks (on_complete, on_error_, on_cancel), this value will not be
   * equal to {@link EnvoyFinalStreamIntel#getReceivedByteCount()}. The latter represents the real
   * number of bytes received before decompression. getConsumedBytesFromResponse() omits the number
   * number of bytes related to the Status Line, and is after decompression.
   */
  public long getConsumedBytesFromResponse();
}
