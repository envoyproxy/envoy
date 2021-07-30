package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import java.nio.ByteBuffer

/**
 * A type representing a stream that is actively transferring data.
 *
 * Constructed using `StreamPrototype`, and used to write to the network.
 */
open class Stream(
  private val underlyingStream: EnvoyHTTPStream
) {
  /**
   * Send headers over the stream.
   *
   * @param headers Headers to send over the stream.
   * @param endStream Whether this is a headers-only request.
   * @return This stream, for chaining syntax.
   */
  open fun sendHeaders(headers: RequestHeaders, endStream: Boolean): Stream {
    underlyingStream.sendHeaders(headers.allHeaders(), endStream)
    return this
  }

  /**
   * Read data from the response stream. Returns immediately.
   *
   * @param byteCount Maximum number of bytes that may be be passed by the next data callback.
   * @return This stream, for chaining syntax.
   */
  open fun readData(byteCount: Long): Stream {
    underlyingStream.readData(byteCount)
    return this
  }

  /**
   * For sending data to an associated stream.
   *
   * @param data Data to send over the stream.
   * @return This stream, for chaining syntax.
   */
  open fun sendData(data: ByteBuffer): Stream {
    underlyingStream.sendData(data, false)
    return this
  }

  /**
   * Close the stream with trailers.
   *
   * @param trailers Trailers with which to close the stream.
   */
  open fun close(trailers: RequestTrailers) {
    underlyingStream.sendTrailers(trailers.allHeaders())
  }

  /**
   * Close the stream with a data frame.
   *
   * @param data Data with which to close the stream.
   */
  open fun close(data: ByteBuffer) {
    underlyingStream.sendData(data, true)
  }

  /**
   * Cancel the stream.
   */
  open fun cancel() {
    underlyingStream.cancel()
  }
}
