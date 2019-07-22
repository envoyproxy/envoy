package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

interface StreamEmitter {
  /**
   * Identify if the emitter is still active and can perform communications with an associated stream.
   *
   * @return true if the stream is active.
   */
  fun isActive(): Boolean

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
   * @param trailers to send with ending a stream. If no trailers are needed, empty map will be the default.
   * @throws IllegalStateException when the stream is not active.
   * @throws EnvoyException when there is an exception ending the stream or sending trailers.
   */
  @Throws(EnvoyException::class)
  fun close(trailers: Map<String, List<String>> = emptyMap())

  /**
   * For cancelling and ending an associated stream.
   *
   * @throws EnvoyException when there is an exception cancelling the stream.
   */
  @Throws(EnvoyException::class)
  fun cancel()
}
