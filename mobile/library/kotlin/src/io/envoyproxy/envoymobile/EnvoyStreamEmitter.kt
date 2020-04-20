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

  override fun close(trailers: Map<String, List<String>>) {
    stream.sendTrailers(trailers)
  }

  override fun close(byteBuffer: ByteBuffer) {
    stream.sendData(byteBuffer, true)
  }

  override fun cancel() {
    stream.cancel()
  }
}
