package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel

/**
 * Exposes one time HTTP stream metrics, context, and other details.
 *
 * Note: a timestamp field (ends with "Ms") with a value of -1 indicates that it is absent.
 *
 * @param streamId The stream identifier.
 * @param connectionId The connection identifier.
 * @param attemptCount The number of attempts used to perform a given request.
 * @param streamStartMs The time the stream started (a.k.a. request started), in ms since the epoch.
 * @param dnsStartMs The time the DNS resolution for this request started, in ms since the epoch.
 * @param dnsEndMs The time the DNS resolution for this request completed, in ms since the epoch.
 * @param connectStartMs The time the upstream connection started, in ms since the epoch. This may
 *   not be set if socketReused is false.
 * @param connectEndMs The time the upstream connection completed, in ms since the epoch. This may
 *   not be set if socketReused is false.
 * @param sslStartMs The time the SSL handshake started, in ms since the epoch. This may not be set
 *   if socketReused is false.
 * @param sslEndMs The time the SSL handshake completed, in ms since the epoch. This may not be set
 *   if socketReused is false.
 * @param sendingStartMs The time the first byte of the request was sent upstream, in ms since the
 *   epoch.
 * @param sendingEndMs The time the last byte of the request was sent upstream, in ms since the
 *   epoch.
 * @param responseStartMs The time the first byte of the response was received, in ms since the
 *   epoch.
 * @param streamEndMs The time when the stream reached a final state (Error, Cancel, Success), in ms
 *   since the epoch.
 * @param socketReused True if the upstream socket had been used previously.
 * @param sentByteCount The number of bytes sent upstream.
 * @param receivedByteCount The number of bytes received from upstream.
 * @param responseFlags The response flags for the stream.
 */
@Suppress("LongParameterList")
class FinalStreamIntel(
  streamId: Long,
  connectionId: Long,
  attemptCount: Long,
  val streamStartMs: Long,
  val dnsStartMs: Long,
  val dnsEndMs: Long,
  val connectStartMs: Long,
  val connectEndMs: Long,
  val sslStartMs: Long,
  val sslEndMs: Long,
  val sendingStartMs: Long,
  val sendingEndMs: Long,
  val responseStartMs: Long,
  val streamEndMs: Long,
  val socketReused: Boolean,
  val sentByteCount: Long,
  val receivedByteCount: Long,
  val responseFlags: Long
) : StreamIntel(streamId, connectionId, attemptCount) {
  constructor(
    superBase: EnvoyStreamIntel,
    base: EnvoyFinalStreamIntel
  ) : this(
    superBase.streamId,
    superBase.connectionId,
    superBase.attemptCount,
    base.streamStartMs,
    base.dnsStartMs,
    base.dnsEndMs,
    base.connectStartMs,
    base.connectEndMs,
    base.sslStartMs,
    base.sslEndMs,
    base.sendingStartMs,
    base.sendingEndMs,
    base.responseStartMs,
    base.streamEndMs,
    base.socketReused,
    base.sentByteCount,
    base.receivedByteCount,
    base.responseFlags
  )
}
