package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import java.nio.ByteBuffer
import java.util.concurrent.Executor

/**
 * A collection of platform-level callbacks that are specified by consumers
 * who wish to interact with streams.
 *
 * `StreamCallbacks` are bridged through to `EnvoyHTTPCallbacks` to communicate with the engine.
 */
internal class StreamCallbacks {
  var onHeaders: ((headers: ResponseHeaders, endStream: Boolean) -> Unit)? = null
  var onData: ((data: ByteBuffer, endStream: Boolean) -> Unit)? = null
  var onTrailers: ((trailers: ResponseTrailers) -> Unit)? = null
  var onCancel: (() -> Unit)? = null
  var onError: ((error: EnvoyError) -> Unit)? = null
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

  override fun onHeaders(headers: Map<String, List<String>>, endStream: Boolean) {
    callbacks.onHeaders?.invoke(ResponseHeaders(headers), endStream)
  }

  override fun onData(byteBuffer: ByteBuffer, endStream: Boolean) {
    callbacks.onData?.invoke(byteBuffer, endStream)
  }

  override fun onTrailers(trailers: Map<String, List<String>>) {
    callbacks.onTrailers?.invoke(ResponseTrailers((trailers)))
  }

  override fun onError(errorCode: Int, message: String, attemptCount: Int) {
    callbacks.onError?.invoke(EnvoyError(errorCode, message, attemptCount))
  }

  override fun onCancel() {
    callbacks.onCancel?.invoke()
  }
}
