package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyObserver
import java.nio.ByteBuffer
import java.util.concurrent.Executor;


/**
 * Callback interface for receiving stream events.
 */
class ResponseHandler(val executor: Executor) {

  class EnvoyObserverAdapter(
      internal val responseHandler: ResponseHandler
  ) : EnvoyObserver {

    override fun getExecutor() : Executor = responseHandler.executor

    override fun onHeaders(headers: Map<String, List<String>>?, endStream: Boolean) {
      val statusCode = headers!![":status"]?.first()?.toIntOrNull() ?: 0
      responseHandler.onHeadersClosure(headers, statusCode, endStream)
    }

    override fun onData(byteBuffer: ByteBuffer?, endStream: Boolean) {
      responseHandler.onDataClosure(byteBuffer, endStream)
    }

    override fun onMetadata(metadata: Map<String, List<String>>?) {
      responseHandler.onMetadataClosure(metadata!!)
    }

    override fun onTrailers(trailers: Map<String, List<String>>?) {
      responseHandler.onTrailersClosure(trailers!!)
    }

    override fun onError() {
      responseHandler.onErrorClosure()
    }

    override fun onCancel() {
      responseHandler.onCancelClosure()
    }
  }

  internal val underlyingObserver = EnvoyObserverAdapter(this)

  private var onHeadersClosure: (headers: Map<String, List<String>>, statusCode: Int, endStream: Boolean) -> Unit = { _, _, _ -> Unit }
  private var onDataClosure: (byteBuffer: ByteBuffer?, endStream: Boolean) -> Unit = { _, _ -> Unit }
  private var onMetadataClosure: (metadata: Map<String, List<String>>) -> Unit = { Unit }
  private var onTrailersClosure: (trailers: Map<String, List<String>>) -> Unit = { Unit }
  private var onErrorClosure: () -> Unit = { Unit }
  private var onCancelClosure: () -> Unit = { Unit }

  /**
   * Specify a callback for when response headers are received by the stream.
   * If `endStream` is `true`, the stream is complete.
   *
   * @param closure: Closure which will receive the headers, status code,
   *                 and flag indicating if the stream is complete.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onHeaders(closure: (headers: Map<String, List<String>>, statusCode: Int, endStream: Boolean) -> Unit): ResponseHandler {
    this.onHeadersClosure = closure
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
  fun onData(closure: (byteBuffer: ByteBuffer?, endStream: Boolean) -> Unit): ResponseHandler {
    this.onDataClosure = closure
    return this
  }

  /**
   * Called when response metadata is received by the stream.
   *
   * @param metadata the metadata of a response.
   * @param endStream true if the stream is complete.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onMetadata(closure: (metadata: Map<String, List<String>>) -> Unit): ResponseHandler {
    this.onMetadataClosure = closure
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
    this.onTrailersClosure = closure
    return this
  }

  /**
   * Specify a callback for when an internal Envoy exception occurs with the stream.
   * If the closure is called, the stream is complete.
   *
   * @param closure: Closure which will be called when an error occurs.
   * @return ResponseHandler, this ResponseHandler.
   */
  fun onError(closure: () -> Unit): ResponseHandler {
    this.onErrorClosure = closure
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
    this.onCancelClosure = closure
    return this
  }
}
