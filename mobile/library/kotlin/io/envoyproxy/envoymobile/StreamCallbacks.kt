package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel
import java.nio.ByteBuffer
import java.util.concurrent.Executor

typealias StreamIntel = EnvoyStreamIntel

/**
 * A collection of platform-level callbacks that are specified by consumers
 * who wish to interact with streams.
 *
 * `StreamCallbacks` are bridged through to `EnvoyHTTPCallbacks` to communicate with the engine.
 */
internal class StreamCallbacks {
  var onHeaders: (
    (headers: ResponseHeaders, endStream: Boolean, streamIntel: StreamIntel) -> Unit
  )? = null
  var onData: ((data: ByteBuffer, endStream: Boolean, streamIntel: StreamIntel) -> Unit)? = null
  var onTrailers: ((trailers: ResponseTrailers, streamIntel: StreamIntel) -> Unit)? = null
  var onCancel: ((streamIntel: StreamIntel) -> Unit)? = null
  var onError: ((error: EnvoyError, streamIntel: StreamIntel) -> Unit)? = null
}

/**
 * Class responsible for bridging between the platform-level `StreamCallbacks` and the
 * engine's `EnvoyHTTPCallbacks`.
 */
internal class EnvoyHTTPCallbacksAdapter(
  private val executor: Executor,
  private val callbacks: StreamCallbacks
) : EnvoyHTTPCallbacks {
  override fun getExecutor(): Executor {
    return executor
  }

  override fun onHeaders(
    headers: Map<String, List<String>>,
    endStream: Boolean,
    streamIntel: StreamIntel
  ) {
    callbacks.onHeaders?.invoke(ResponseHeaders(headers), endStream, streamIntel)
  }

  override fun onData(byteBuffer: ByteBuffer, endStream: Boolean, streamIntel: StreamIntel) {
    callbacks.onData?.invoke(byteBuffer, endStream, streamIntel)
  }

  override fun onTrailers(trailers: Map<String, List<String>>, streamIntel: StreamIntel) {
    callbacks.onTrailers?.invoke(ResponseTrailers((trailers)), streamIntel)
  }

  override fun onError(
    errorCode: Int,
    message: String,
    attemptCount: Int,
    streamIntel: StreamIntel
  ) {
    callbacks.onError?.invoke(EnvoyError(errorCode, message, attemptCount), streamIntel)
  }

  override fun onCancel(streamIntel: StreamIntel) {
    callbacks.onCancel?.invoke(streamIntel)
  }
}
