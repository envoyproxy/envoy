package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import java.nio.ByteBuffer

class EnvoyStreamEmitter(
  private val stream: EnvoyHTTPStream
) : StreamEmitter {

  /**
   * For sending data to an associated stream.
   *
   * @param byteBuffer the byte buffer data to send to the stream.
   * @return StreamEmitter, this stream emitter.
   */
  override fun sendData(byteBuffer: ByteBuffer): StreamEmitter {
    stream.sendData(byteBuffer, false)
    return this
  }

  /**
   * For sending a map of metadata to an associated stream.
   *
   * @param metadata the metadata to send over the stream.
   * @return StreamEmitter, this stream emitter.
   */
  override fun sendMetadata(metadata: Map<String, List<String>>): StreamEmitter {
    stream.sendMetadata(metadata)
    return this
  }

  /**
   * For ending an associated stream and sending trailers.
   *
   * @param trailers to send with ending a stream. If null, stream will be closed with an empty data frame.
   */
  override fun close(trailers: Map<String, List<String>>?) {
    trailers?.let {
      stream.sendTrailers(it)
      return
    }
    stream.sendData(ByteBuffer.allocate(0), true)
  }

  /**
   * For cancelling and ending an associated stream.
   */
  override fun cancel() {
    stream.cancel()
  }
}
