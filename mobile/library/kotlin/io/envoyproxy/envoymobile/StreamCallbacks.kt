package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.ByteBuffers
import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel
import java.nio.ByteBuffer
import java.util.concurrent.Executor

/**
 * A collection of platform-level callbacks that are specified by consumers who wish to interact
 * with streams.
 *
 * `StreamCallbacks` are bridged through to `EnvoyHTTPCallbacks` to communicate with the engine.
 */
class StreamCallbacks {
  var onHeaders:
    ((headers: ResponseHeaders, endStream: Boolean, streamIntel: StreamIntel) -> Unit)? =
    null
  var onData: ((data: ByteBuffer, endStream: Boolean, streamIntel: StreamIntel) -> Unit)? = null
  var onTrailers: ((trailers: ResponseTrailers, streamIntel: StreamIntel) -> Unit)? = null
  var onCancel: ((finalStreamIntel: FinalStreamIntel) -> Unit)? = null
  var onError: ((error: EnvoyError, finalStreamIntel: FinalStreamIntel) -> Unit)? = null
  var onSendWindowAvailable: ((streamIntel: StreamIntel) -> Unit)? = null
  var onComplete: ((finalStreamIntel: FinalStreamIntel) -> Unit)? = null
}

/**
 * Class responsible for bridging between the platform-level `StreamCallbacks` and the engine's
 * `EnvoyHTTPCallbacks`.
 */
class EnvoyHTTPCallbacksAdapter(
  private val executor: Executor?,
  private val callbacks: StreamCallbacks
) : EnvoyHTTPCallbacks {
  override fun onHeaders(
    headers: Map<String, List<String>>,
    endStream: Boolean,
    streamIntel: EnvoyStreamIntel
  ) {
    if (executor != null) {
      executor.execute {
        callbacks.onHeaders?.invoke(ResponseHeaders(headers), endStream, StreamIntel(streamIntel))
      }
    } else {
      callbacks.onHeaders?.invoke(ResponseHeaders(headers), endStream, StreamIntel(streamIntel))
    }
  }

  override fun onData(byteBuffer: ByteBuffer, endStream: Boolean, streamIntel: EnvoyStreamIntel) {
    if (executor != null) {
      // The `ByteBuffer` passed into `onData` is a direct `ByteBuffer` managed by the native code
      // and it will be destroyed upon completing the `onData` call. We need to copy the
      // `ByteBuffer` when executing the `onData` in a separate thread to avoid a dangling
      // reference.
      val copiedBuffer = ByteBuffers.copy(byteBuffer)
      executor.execute {
        callbacks.onData?.invoke(copiedBuffer, endStream, StreamIntel(streamIntel))
      }
    } else {
      callbacks.onData?.invoke(byteBuffer, endStream, StreamIntel(streamIntel))
    }
  }

  override fun onTrailers(trailers: Map<String, List<String>>, streamIntel: EnvoyStreamIntel) {
    if (executor != null) {
      executor.execute {
        callbacks.onTrailers?.invoke(ResponseTrailers((trailers)), StreamIntel(streamIntel))
      }
    } else {
      callbacks.onTrailers?.invoke(ResponseTrailers((trailers)), StreamIntel(streamIntel))
    }
  }

  override fun onError(
    errorCode: Int,
    message: String,
    attemptCount: Int,
    streamIntel: EnvoyStreamIntel,
    finalStreamIntel: EnvoyFinalStreamIntel
  ) {
    if (executor != null) {
      executor.execute {
        callbacks.onError?.invoke(
          EnvoyError(errorCode, message, attemptCount),
          FinalStreamIntel(streamIntel, finalStreamIntel)
        )
      }
    } else {
      callbacks.onError?.invoke(
        EnvoyError(errorCode, message, attemptCount),
        FinalStreamIntel(streamIntel, finalStreamIntel)
      )
    }
  }

  override fun onCancel(streamIntel: EnvoyStreamIntel, finalStreamIntel: EnvoyFinalStreamIntel) {
    if (executor != null) {
      executor.execute {
        callbacks.onCancel?.invoke(FinalStreamIntel(streamIntel, finalStreamIntel))
      }
    } else {
      callbacks.onCancel?.invoke(FinalStreamIntel(streamIntel, finalStreamIntel))
    }
  }

  override fun onSendWindowAvailable(streamIntel: EnvoyStreamIntel) {
    if (executor != null) {
      executor.execute { callbacks.onSendWindowAvailable?.invoke(StreamIntel(streamIntel)) }
    } else {
      callbacks.onSendWindowAvailable?.invoke(StreamIntel(streamIntel))
    }
  }

  override fun onComplete(streamIntel: EnvoyStreamIntel, finalStreamIntel: EnvoyFinalStreamIntel) {
    if (executor != null) {
      executor.execute {
        callbacks.onComplete?.invoke(FinalStreamIntel(streamIntel, finalStreamIntel))
      }
    } else {
      callbacks.onComplete?.invoke(FinalStreamIntel(streamIntel, finalStreamIntel))
    }
  }
}
