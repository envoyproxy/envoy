package io.envoyproxy.envoymobile.engine.types;

/**
 * Exposes internal HTTP stream metrics, context, and other details sent once on stream end.
 *
 * Note: a value of -1 means "not present" for any field where the name is suffixed with "Ms".
 */
public interface EnvoyFinalStreamIntel {
  /*
   * The time the stream started (a.k.a request started), in ms since the epoch.
   */
  long getStreamStartMs();
  /*
   * The time the DNS resolution for this request started, in ms since the epoch.
   */
  long getDnsStartMs();
  /*
   * The time the DNS resolution for this request completed, in ms since the epoch.
   */
  long getDnsEndMs();
  /*
   * The time the upstream connection started, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  long getConnectStartMs();
  /*
   * The time the upstream connection completed, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  long getConnectEndMs();
  /*
   * The time the SSL handshake started, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  long getSslStartMs();
  /*
   * The time the SSL handshake completed, in ms since the epoch.
   * This may not be set if socket_reused is false.
   */
  long getSslEndMs();
  /*
   * The time the first byte of the request was sent upstream, in ms since the epoch.
   */
  long getSendingStartMs();
  /*
   * The time the last byte of the request was sent upstream, in ms since the epoch.
   */
  long getSendingEndMs();
  /*
   * The time the first byte of the response was received, in ms since the epoch.
   */
  long getResponseStartMs();
  /*
   * The time when the stream reached a final state (Error, Cancel, Success), in ms since the epoch.
   */
  long getStreamEndMs();
  /*
   * True if the upstream socket had been used previously.
   */
  boolean getSocketReused();
  /*
   * The number of bytes sent upstream.
   */
  long getSentByteCount();
  /*
   * The number of bytes received from upstream.
   */
  long getReceivedByteCount();
  /*
   * The response flags for the stream. See
   * https://github.com/envoyproxy/envoy/blob/main/envoy/stream_info/stream_info.h#L39
   * for values.
   */
  long getResponseFlags();

  /* The protocol for the upstream stream, if one was established, else -1 See
   * https://github.com/envoyproxy/envoy/blob/main/envoy/http/protocol.h#L39 for values. */
  long getUpstreamProtocol();
}
