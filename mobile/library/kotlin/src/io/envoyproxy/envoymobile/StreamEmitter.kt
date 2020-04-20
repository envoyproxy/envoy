package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

/**
 * Interface for a stream that may be canceled.
 */
interface CancelableStream {
  /**
   * Cancel and end the associated stream.
   */
  fun cancel()
}

/**
 * Interface allowing for sending/emitting data on an Envoy stream.
 */
interface StreamEmitter : CancelableStream {
  /**
   * For sending data to an associated stream.
   *
   * @param byteBuffer the byte buffer data to send to the stream.
   * @return this stream emitter.
   */
  fun sendData(byteBuffer: ByteBuffer): StreamEmitter

  /**
   * For sending a map of metadata to an associated stream.
   *
   * @param metadata the metadata to send over the stream.
   * @return this stream emitter.
   */
  fun sendMetadata(metadata: Map<String, List<String>>): StreamEmitter

  /**
   * Close the stream with trailers.
   *
   * @param trailers trailers with which to close the stream.
   */
  fun close(trailers: Map<String, List<String>>)

  /**
   * Close the stream with a data frame.
   *
   * @param byteBuffer data with which to close the stream.
   */
  fun close(byteBuffer: ByteBuffer)
}
