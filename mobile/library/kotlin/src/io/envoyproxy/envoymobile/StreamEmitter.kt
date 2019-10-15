package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

/**
 * Interface for a stream that may be canceled.
 */
interface CancelableStream {
  /**
   * Cancel and end the associated stream.
   * @throws EnvoyException when there is an exception canceling the stream or sending trailers.
   */
  @Throws(EnvoyException::class)
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
   * @throws IllegalStateException when the stream is not active
   * @throws EnvoyException when there is an exception sending data.
   * @return this stream emitter.
   */
  @Throws(EnvoyException::class)
  fun sendData(byteBuffer: ByteBuffer): StreamEmitter

  /**
   * For sending a map of metadata to an associated stream.
   *
   * @param metadata the metadata to send over the stream.
   * @throws IllegalStateException when the stream is not active.
   * @throws EnvoyException when there is an exception sending metadata.
   * @return this stream emitter.
   */
  @Throws(EnvoyException::class)
  fun sendMetadata(metadata: Map<String, List<String>>): StreamEmitter

  /**
   * For ending an associated stream and sending trailers.
   *
   * @param trailers to send with ending a stream. If null, stream will be closed with an empty data frame.
   * @throws IllegalStateException when the stream is not active.
   * @throws EnvoyException when there is an exception ending the stream or sending trailers.
   */
  @Throws(EnvoyException::class)
  fun close(trailers: Map<String, List<String>>?)
}
