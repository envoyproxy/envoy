package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine

/**
 * Envoy implementation of `StreamClient`.
 */
internal class StreamClientImpl constructor(
  internal val engine: EnvoyEngine
) : StreamClient {

  override fun newStreamPrototype() = StreamPrototype(engine)
}
