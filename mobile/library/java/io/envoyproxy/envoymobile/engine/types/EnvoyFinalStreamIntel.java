package io.envoyproxy.envoymobile.engine.types;

import java.util.Objects;

/**
 * Exposes internal HTTP stream metrics, context, and other details sent once on stream end.
 *
 * Note: a value of -1 means "not present" for any field where the name is suffixed with "Ms".
 */
public class EnvoyFinalStreamIntel {
  private final long streamStartMs;
  private final long dnsStartMs;
  private final long dnsEndMs;
  private final long connectStartMs;
  private final long connectEndMs;
  private final long sslStartMs;
  private final long sslEndMs;
  private final long sendingStartMs;
  private final long sendingEndMs;
  private final long responseStartMs;
  private final long streamEndMs;
  private final boolean socketReused;
  private final long sentByteCount;
  private final long receivedByteCount;
  private final long responseFlags;
  private final long upstreamProtocol;

  public EnvoyFinalStreamIntel(long streamStartMs, long dnsStartMs, long dnsEndMs,
                               long connectStartMs, long connectEndMs, long sslStartMs,
                               long sslEndMs, long sendingStartMs, long sendingEndMs,
                               long responseStartMs, long streamEndMs, boolean socketReused,
                               long sentByteCount, long receivedByteCount, long responseFlags,
                               long upstreamProtocol) {
    this.streamStartMs = streamStartMs;
    this.dnsStartMs = dnsStartMs;
    this.dnsEndMs = dnsEndMs;
    this.connectStartMs = connectStartMs;
    this.connectEndMs = connectEndMs;
    this.sslStartMs = sslStartMs;
    this.sslEndMs = sslEndMs;
    this.sendingStartMs = sendingStartMs;
    this.sendingEndMs = sendingEndMs;
    this.responseStartMs = responseStartMs;
    this.streamEndMs = streamEndMs;
    this.socketReused = socketReused;
    this.sentByteCount = sentByteCount;
    this.receivedByteCount = receivedByteCount;
    this.responseFlags = responseFlags;
    this.upstreamProtocol = upstreamProtocol;
  }

  public EnvoyFinalStreamIntel(long[] values) {
    streamStartMs = values[0];
    dnsStartMs = values[1];
    dnsEndMs = values[2];
    connectStartMs = values[3];
    connectEndMs = values[4];
    sslStartMs = values[5];
    sslEndMs = values[6];
    sendingStartMs = values[7];
    sendingEndMs = values[8];
    responseStartMs = values[9];
    streamEndMs = values[10];
    socketReused = values[11] != 0;
    sentByteCount = values[12];
    receivedByteCount = values[13];
    responseFlags = values[14];
    upstreamProtocol = values[15];
  }

  /**
   * The time the stream started (a.k.a request started), in ms since the epoch.
   */
  public long getStreamStartMs() { return streamStartMs; }

  /**
   * The time the DNS resolution for this request started, in ms since the epoch.
   */
  public long getDnsStartMs() { return dnsStartMs; }

  /**
   * The time the DNS resolution for this request completed, in ms since the epoch.
   */
  public long getDnsEndMs() { return dnsEndMs; }

  /**
   * The time the upstream connection started, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getConnectStartMs() { return connectStartMs; }

  /**
   * The time the upstream connection completed, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getConnectEndMs() { return connectEndMs; }

  /**
   * The time the SSL handshake started, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getSslStartMs() { return sslStartMs; }

  /**
   * The time the SSL handshake completed, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getSslEndMs() { return sslEndMs; }

  /**
   * The time the first byte of the request was sent upstream, in ms since the epoch.
   */
  public long getSendingStartMs() { return sendingStartMs; }

  /**
   * The time the last byte of the request was sent upstream, in ms since the epoch.
   */
  public long getSendingEndMs() { return sendingEndMs; }

  /**
   * The time the first byte of the response was received, in ms since the epoch.
   */
  public long getResponseStartMs() { return responseStartMs; }

  /**
   * The time when the stream reached a final state (Error, Cancel, Success), in ms since the epoch.
   */
  public long getStreamEndMs() { return streamEndMs; }

  /**
   * True if the upstream socket had been used previously.
   */
  public boolean getSocketReused() { return socketReused; }

  /**
   * The number of bytes sent upstream.
   */
  public long getSentByteCount() { return sentByteCount; }

  /**
   * The number of bytes received from upstream.
   */
  public long getReceivedByteCount() { return receivedByteCount; }

  /*
   * The response flags for the stream. See
   * https://github.com/envoyproxy/envoy/blob/main/envoy/stream_info/stream_info.h#L39
   * for values.
   */
  public long getResponseFlags() { return responseFlags; }

  /**
   * The protocol for the upstream stream, if one was established, else -1 See
   * https://github.com/envoyproxy/envoy/blob/main/envoy/http/protocol.h#L39 for values.
   */
  public long getUpstreamProtocol() { return upstreamProtocol; }

  @Override
  public boolean equals(Object object) {
    if (this == object) {
      return true;
    }
    if (object == null || getClass() != object.getClass()) {
      return false;
    }
    EnvoyFinalStreamIntel finalStreamIntel = (EnvoyFinalStreamIntel)object;
    return streamStartMs == finalStreamIntel.streamStartMs &&
        dnsStartMs == finalStreamIntel.dnsStartMs && dnsEndMs == finalStreamIntel.dnsEndMs &&
        connectStartMs == finalStreamIntel.connectStartMs &&
        connectEndMs == finalStreamIntel.connectEndMs &&
        sslStartMs == finalStreamIntel.sslStartMs && sslEndMs == finalStreamIntel.sslEndMs &&
        sendingStartMs == finalStreamIntel.sendingStartMs &&
        sendingEndMs == finalStreamIntel.sendingEndMs &&
        responseStartMs == finalStreamIntel.responseStartMs &&
        streamEndMs == finalStreamIntel.streamEndMs &&
        socketReused == finalStreamIntel.socketReused &&
        sentByteCount == finalStreamIntel.sentByteCount &&
        receivedByteCount == finalStreamIntel.receivedByteCount &&
        responseFlags == finalStreamIntel.responseFlags &&
        upstreamProtocol == finalStreamIntel.upstreamProtocol;
  }

  @Override
  public int hashCode() {
    return Objects.hash(streamStartMs, dnsStartMs, dnsEndMs, connectStartMs, connectEndMs,
                        sslStartMs, sslEndMs, sendingStartMs, sendingEndMs, responseStartMs,
                        streamEndMs, socketReused, sentByteCount, receivedByteCount, responseFlags,
                        upstreamProtocol);
  }
}
