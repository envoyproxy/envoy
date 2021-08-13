package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel
import java.nio.ByteBuffer
import java.util.concurrent.Executor

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
    streamIntel: EnvoyStreamIntel
  ) {
    callbacks.onHeaders?.invoke(ResponseHeaders(headers), endStream, StreamIntel(streamIntel))
  }

  override fun onData(byteBuffer: ByteBuffer, endStream: Boolean, streamIntel: EnvoyStreamIntel) {
    callbacks.onData?.invoke(byteBuffer, endStream, StreamIntel(streamIntel))
  }

  override fun onTrailers(trailers: Map<String, List<String>>, streamIntel: EnvoyStreamIntel) {
    callbacks.onTrailers?.invoke(ResponseTrailers((trailers)), StreamIntel(streamIntel))
  }

  override fun onError(
    errorCode: Int,
    message: String,
    attemptCount: Int,
    streamIntel: EnvoyStreamIntel
  ) {
    callbacks.onError?.invoke(
      EnvoyError(errorCode, message, attemptCount),
      StreamIntel(streamIntel)
    )
  }

  override fun onCancel(streamIntel: EnvoyStreamIntel) {
    callbacks.onCancel?.invoke(StreamIntel(streamIntel))
  }
}
