package io.envoyproxy.envoymobile.engine.types;

import java.util.Objects;

/**
 * Exposes internal HTTP stream metrics, context, and other details.
 */
public class EnvoyStreamIntel {
  private final long streamId;
  private final long connectionId;
  private final long attemptCount;
  private final long consumedBytesFromResponse;

  public EnvoyStreamIntel(long streamId, long connectionId, long attemptCount,
                          long consumedBytesFromResponse) {
    this.streamId = streamId;
    this.connectionId = connectionId;
    this.attemptCount = attemptCount;
    this.consumedBytesFromResponse = consumedBytesFromResponse;
  }

  public EnvoyStreamIntel(long[] values) {
    streamId = values[0];
    connectionId = values[1];
    attemptCount = values[2];
    consumedBytesFromResponse = values[3];
  }

  /**
   * An internal identifier for the stream.
   */
  public long getStreamId() { return streamId; }

  /**
   * An internal identifier for the connection carrying the stream.
   */
  public long getConnectionId() { return connectionId; }

  /**
   * The number of internal attempts to carry out a request/operation.
   */
  public long getAttemptCount() { return attemptCount; }

  /**
   * The number of bytes consumed by the non terminal callbacks, from the response.
   *
   * <p>>NOTE: on terminal callbacks (on_complete, on_error_, on_cancel), this value will not be
   * equal to {@link EnvoyFinalStreamIntel#getReceivedByteCount()}. The latter represents the real
   * number of bytes received before decompression. getConsumedBytesFromResponse() omits the number
   * number of bytes related to the Status Line, and is after decompression.
   */
  public long getConsumedBytesFromResponse() { return consumedBytesFromResponse; }

  @Override
  public boolean equals(Object object) {
    if (this == object) {
      return true;
    }
    if (object == null || getClass() != object.getClass()) {
      return false;
    }
    EnvoyStreamIntel streamIntel = (EnvoyStreamIntel)object;
    return streamId == streamIntel.streamId && connectionId == streamIntel.connectionId &&
        attemptCount == streamIntel.attemptCount &&
        consumedBytesFromResponse == streamIntel.consumedBytesFromResponse;
  }

  @Override
  public int hashCode() {
    return Objects.hash(streamId, connectionId, attemptCount, consumedBytesFromResponse);
  }
}
