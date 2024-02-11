package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.nio.ByteBuffer
import java.util.concurrent.Executor
import java.util.concurrent.Executors

/**
 * A type representing a stream that has not yet been started.
 *
 * Constructed via `StreamClient`, and used to assign response callbacks prior to starting an
 * `Stream` by calling `start()`.
 *
 * @param engine Engine to use for starting streams.
 */
open class StreamPrototype(private val engine: EnvoyEngine) {
  private val callbacks = StreamCallbacks()
  private var explicitFlowControl = false
  private var useByteBufferPosition = false

  /**
   * Start a new stream.
   *
   * @param executor Executor on which to receive callback events.
   * @return The new stream.
   */
  open fun start(executor: Executor = Executors.newSingleThreadExecutor()): Stream {
    val engineStream = engine.startStream(createCallbacks(executor), explicitFlowControl)
    return Stream(engineStream, useByteBufferPosition)
  }

  /**
   * Allows explicit flow control to be enabled. When flow control is enabled, the owner of a stream
   * is responsible for providing a buffer to receive response body data. If the buffer is smaller
   * than the amount of data available, response callbacks will halt, and the underlying network
   * protocol may signal for the server to stop sending data, until more space is available. This
   * can limit the memory consumed by a server response, but may also result in reduced overall
   * throughput, depending on usage.
   *
   * @param enabled Whether explicit flow control will be enabled for the stream.
   * @return This stream, for chaining syntax.
   */
  fun setExplicitFlowControl(enabled: Boolean): StreamPrototype {
    this.explicitFlowControl = enabled
    return this
  }

  /**
   * Specify how to determine the length of data to send for a given ByteBuffer.
   *
   * @param enabled Use ByteBuffer's position when true, otherwise use its capacity.
   * @return This stream, for chaining syntax.
   */
  fun setUseByteBufferPosition(enabled: Boolean): StreamPrototype {
    this.useByteBufferPosition = enabled
    return this
  }

  /**
   * Specify a callback for when response headers are received by the stream. If `endStream` is
   * `true`, the stream is complete, pending an onComplete callback.
   *
   * @param closure Closure which will receive the headers and flag indicating if the stream is
   *   headers-only.
   * @return This stream, for chaining syntax.
   */
  fun setOnResponseHeaders(
    closure: (headers: ResponseHeaders, endStream: Boolean, streamIntel: StreamIntel) -> Unit
  ): StreamPrototype {
    callbacks.onHeaders = closure
    return this
  }

  /**
   * Specify a callback for when a data frame is received by the stream. If `endStream` is `true`,
   * the stream is complete, pending an onComplete callback.
   *
   * @param closure Closure which will receive the data and flag indicating whether this is the last
   *   data frame.
   * @return This stream, for chaining syntax.
   */
  fun setOnResponseData(
    closure: (data: ByteBuffer, endStream: Boolean, streamIntel: StreamIntel) -> Unit
  ): StreamPrototype {
    callbacks.onData = closure
    return this
  }

  /**
   * Specify a callback for when trailers are received by the stream. If the closure is called, the
   * stream is complete, pending an onComplete callback.
   *
   * @param closure Closure which will receive the trailers.
   * @return This stream, for chaining syntax.
   */
  fun setOnResponseTrailers(
    closure: (trailers: ResponseTrailers, streamIntel: StreamIntel) -> Unit
  ): StreamPrototype {
    callbacks.onTrailers = closure
    return this
  }

  /**
   * Specify a callback for when an internal Envoy exception occurs with the stream. If the closure
   * is called, the stream is complete.
   *
   * @param closure Closure which will be called when an error occurs.
   * @return This stream, for chaining syntax.
   */
  fun setOnError(
    closure: (error: EnvoyError, finalStreamIntel: FinalStreamIntel) -> Unit
  ): StreamPrototype {
    callbacks.onError = closure
    return this
  }

  /**
   * Specify a callback for when a stream is complete. If the closure is called, the stream is
   * complete.
   *
   * @param closure Closure which will be called when an error occurs.
   * @return This stream, for chaining syntax.
   */
  fun setOnComplete(closure: (finalStreamIntel: FinalStreamIntel) -> Unit): StreamPrototype {
    callbacks.onComplete = closure
    return this
  }

  /**
   * Specify a callback for when the stream is canceled. If the closure is called, the stream is
   * complete.
   *
   * @param closure Closure which will be called when the stream is canceled.
   * @return This stream, for chaining syntax.
   */
  fun setOnCancel(closure: (finalStreamIntel: FinalStreamIntel) -> Unit): StreamPrototype {
    callbacks.onCancel = closure
    return this
  }

  /**
   * Specify a callback for when additional send window becomes available. This is only ever called
   * when the library is in explicit flow control mode. When enabled, the issuer should wait for
   * this callback after calling sendData, before making another call to sendData.
   *
   * @param closure Closure which will be called when additional send window becomes available.
   * @return This stream, for chaining syntax.
   */
  fun setOnSendWindowAvailable(closure: (streamIntel: StreamIntel) -> Unit): StreamPrototype {
    callbacks.onSendWindowAvailable = closure
    return this
  }

  /**
   * Create engine callbacks using the provided queue.
   *
   * @param executor Executor on which to receive callback events.
   * @return A new set of engine callbacks.
   */
  internal fun createCallbacks(executor: Executor): EnvoyHTTPCallbacksAdapter {
    return EnvoyHTTPCallbacksAdapter(executor, callbacks)
  }
}
