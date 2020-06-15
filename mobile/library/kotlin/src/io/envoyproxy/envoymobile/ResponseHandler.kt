package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import java.nio.ByteBuffer
import java.util.concurrent.Executor

/**
 * Callback interface for receiving stream events.
 */
class ResponseHandler(val executor: Executor) {

  class EnvoyHTTPCallbacksAdapter(
    private val executor: Executor
  ) : EnvoyHTTPCallbacks {

    internal var onHeadersClosure: (
      headers: Map<String, List<String>>,
      statusCode: Int,
      endStream: Boolean
    ) -> Unit = { _, _, _ -> Unit }
    internal var onDataClosure: (
      byteBuffer: ByteBuffer,
      endStream: Boolean
    ) -> Unit = { _, _ -> Unit }
    internal var onTrailersClosure: (
      trailers: Map<String, List<String>>
    ) -> Unit = { Unit }
    internal var onErrorClosure: (
      errorCode: Int,
      message: String,
      attemptCount: Int
    ) -> Unit = { _, _, _ -> Unit }
    internal var onCancelClosure: () -> Unit = { Unit }

    override fun getExecutor(): Executor {
      return executor
    }

    override fun onHeaders(headers: Map<String, List<String>>, endStream: Boolean) {
      val statusCode = headers[":status"]?.first()?.toIntOrNull() ?: 0
      onHeadersClosure(headers, statusCode, endStream)
    }

    override fun onData(byteBuffer: ByteBuffer, endStream: Boolean) {
      onDataClosure(byteBuffer, endStream)
    }

    override fun onTrailers(trailers: Map<String, List<String>>) {
      onTrailersClosure(trailers)
    }

    override fun onError(errorCode: Int, message: String, attemptCount: Int) {
      onErrorClosure(errorCode, message, attemptCount)
    }

    override fun onCancel() {
      onCancelClosure()
    }
  }

  internal val underlyingCallbacks = EnvoyHTTPCallbacksAdapter(executor)

  /**
   * Specify a callback for when response headers are received by the stream.
   * If `endStream` is `true`, the stream is complete.
   *
   * @param closure: Closure which will receive the headers, status code,
   *                 and flag indicating if the stream is complete.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onHeaders(
    closure: (headers: Map<String, List<String>>, statusCode: Int, endStream: Boolean) -> Unit
  ): ResponseHandler {
    underlyingCallbacks.onHeadersClosure = closure
    return this
  }

  /**
   * Specify a callback for when a data frame is received by the stream.
   * If `endStream` is `true`, the stream is complete.
   *
   * @param closure: Closure which will receive the data,
   *                 and flag indicating if the stream is complete.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onData(closure: (byteBuffer: ByteBuffer, endStream: Boolean) -> Unit): ResponseHandler {
    underlyingCallbacks.onDataClosure = closure
    return this
  }

  /**
   * Specify a callback for when trailers are received by the stream.
   * If the closure is called, the stream is complete.
   *
   * @param closure: Closure which will receive the trailers.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onTrailers(closure: (trailers: Map<String, List<String>>) -> Unit): ResponseHandler {
    underlyingCallbacks.onTrailersClosure = closure
    return this
  }

  /**
   * Specify a callback for when an internal Envoy exception occurs with the stream.
   * If the closure is called, the stream is complete.
   *
   * @param closure: Closure which will be called when an error occurs.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onError(closure: (error: EnvoyError) -> Unit): ResponseHandler {
    underlyingCallbacks.onErrorClosure = { errorCode, message, attemptCount ->
      closure(EnvoyError(errorCode, message, if (attemptCount < 0) null else attemptCount))
      Unit
    }
    return this
  }

  /**
   * Specify a callback for when the stream is canceled.
   * If the closure is called, the stream is complete.
   *
   * @param closure: Closure which will be called when the stream is canceled.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onCanceled(closure: () -> Unit): ResponseHandler {
    underlyingCallbacks.onCancelClosure = closure
    return this
  }
}
