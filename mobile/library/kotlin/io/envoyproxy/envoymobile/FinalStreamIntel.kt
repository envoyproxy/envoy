package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel

/**
 * Exposes one time HTTP stream metrics, context, and other details.
 * @param requestStartMs The time the request started, in ms since the epoch.
 * @param dnsStartMs The time the DNS resolution for this request started, in ms since the epoch.
 * @param dnsEndMs The time the DNS resolution for this request completed, in ms since the epoch.
 * @param connectStartMsThe time the upstream connection started, in ms since the epoch.
 *        This may not be set if socket_reused is false.
 * @param connectEndMs The time the upstream connection completed, in ms since the epoch.
 *        This may not be set if socket_reused is false.
 * @param sslStartMs The time the SSL handshake started, in ms since the epoch.
 * This may not be set if socket_reused is false.
 * @param sslEndMs The time the SSL handshake completed, in ms since the epoch.
 * This may not be set if socket_reused is false.
 * @param sendingStartMs The time the first byte of the request was sent upstream,
 *        in ms since the epoch.
 * @param sendingEndMs The time the last byte of the request was sent upstream, in ms since the
 *        epoch.
 * @param responseStartMs The time the first byte of the response was received, in ms since the
 *        epoch.
 * @param @param requestEndMs The time the last byte of the request was received, in ms since the
 *        epoch.
 * @param socket_reused True if the upstream socket had been used previously.
 * @param sentByteCount The number of bytes sent upstream.
 * @param receivedByteCount The number of bytes received from upstream.
 */
@Suppress("LongParameterList")
class FinalStreamIntel constructor(
  val requestStartMs: Long,
  val dnsStartMs: Long,
  val dnsEndMs: Long,
  val connectStartMs: Long,
  val connectEndMs: Long,
  val sslStartMs: Long,
  val sslEndMs: Long,
  val sendingStartMs: Long,
  val sendingEndMs: Long,
  val responseStartMs: Long,
  val requestEndMs: Long,
  val socketReused: Boolean,
  val sentByteCount: Long,
  val receivedByteCount: Long
) {
  constructor(base: EnvoyFinalStreamIntel) : this(
    base.requestStartMs, base.dnsStartMs,
    base.dnsEndMs, base.connectStartMs,
    base.connectEndMs, base.sslStartMs,
    base.sslEndMs, base.sendingStartMs,
    base.sendingEndMs,
    base.responseStartMs, base.requestEndMs,
    base.socketReused, base.sentByteCount,
    base.receivedByteCount
  )
}
