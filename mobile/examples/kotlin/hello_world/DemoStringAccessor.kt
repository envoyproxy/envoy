package io.envoyproxy.envoymobile.helloenvoykotlin

import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor
import java.nio.ByteBuffer

class DemoStringAccessor : EnvoyStringAccessor {
  override fun getString(): ByteBuffer {
    return ByteBuffer.wrap("PlatformString".toByteArray(Charsets.UTF_8))
  }
}
