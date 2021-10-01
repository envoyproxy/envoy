package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel

/**
 * Exposes internal HTTP stream metrics, context, and other details.
 * @param streamId An internal identifier for the stream. -1 if not set.
 * @param connectionId An internal identifier for the connection carrying the stream. -1 if not set.
 * @param attemptCount The number of internal attempts to carry out a request/operation. 0 if
 * not set.
 */
class StreamIntel constructor(
  val streamId: Long,
  val connectionId: Long,
  val attemptCount: Long
) {
  constructor(base: EnvoyStreamIntel) : this(base.streamId, base.connectionId, base.attemptCount)
}
