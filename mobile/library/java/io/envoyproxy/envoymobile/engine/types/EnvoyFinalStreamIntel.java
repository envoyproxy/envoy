package io.envoyproxy.envoymobile.engine.types;

/**
 * Exposes internal HTTP stream metrics, context, and other details sent once on stream end.
 */
public interface EnvoyFinalStreamIntel {
  /*
   * The time the request started, in ms since the epoch.
   */
  public long getRequestStartMs();
  /*
   * The time the DNS resolution for this request started, in ms since the epoch.
   */
  public long getDnsStartMs();
  /*
   * The time the DNS resolution for this request completed, in ms since the epoch.
   */
  public long getDnsEndMs();
  /*
   * The time the upstream connection started, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getConnectStartMs();
  /*
   * The time the upstream connection completed, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getConnectEndMs();
  /*
   * The time the SSL handshake started, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getSslStartMs();
  /*
   * The time the SSL handshake completed, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  public long getSslEndMs();
  /*
   * The time the first byte of the request was sent upstream, in ms since the epoch.
   */
  public long getSendingStartMs();
  /*
   * The time the last byte of the request was sent upstream, in ms since the epoch.
   */
  public long getSendingEndMs();
  /*
   * The time the first byte of the response was received, in ms since the epoch.
   */
  public long getResponseStartMs();
  /*
   * The time the last byte of the request was received, in ms since the epoch.
   */
  public long getRequestEndMs();
  /*
   * True if the upstream socket had been used previously.
   */
  public boolean getSocketReused();
  /*
   * The number of bytes sent upstream.
   */
  public long getSentByteCount();
  /*
   * The number of bytes received from upstream.
   */
  public long getReceivedByteCount();
  /*
   * The response flags for the stream. See
   * https://github.com/envoyproxy/envoy/blob/main/envoy/stream_info/stream_info.h#L39
   * for values.
   */
  public long getResponseFlags();
}
