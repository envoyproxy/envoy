package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import java.nio.ByteBuffer

class EnvoyStreamEmitter(
  private val stream: EnvoyHTTPStream
) : StreamEmitter {

  override fun sendData(byteBuffer: ByteBuffer): StreamEmitter {
    stream.sendData(byteBuffer, false)
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
