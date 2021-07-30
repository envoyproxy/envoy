package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import java.nio.ByteBuffer

/**
 * Internal no-op mock implementation of the engine's `EnvoyHTTPStream`.
 *
 * @param callbacks Callbacks associated with the stream.
 */
internal class MockEnvoyHTTPStream(
  val callbacks: EnvoyHTTPCallbacks,
  val explicitFlowControl: Boolean
) : EnvoyHTTPStream(0, callbacks, explicitFlowControl) {
  override fun sendHeaders(headers: MutableMap<String, MutableList<String>>?, endStream: Boolean) {}

  override fun sendData(data: ByteBuffer?, endStream: Boolean) {}

  override fun readData(byteCount: Long) {}

  override fun sendTrailers(trailers: MutableMap<String, MutableList<String>>?) {}

  override fun cancel(): Int {
    return 0
  }
}
