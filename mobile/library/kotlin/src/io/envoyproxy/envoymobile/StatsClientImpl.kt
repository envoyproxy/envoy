package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine

/**
 * Envoy implementation of `StatsClient`.
 */
internal class StatsClientImpl constructor(
  internal val engine: EnvoyEngine
) : StatsClient {

  override fun counter(vararg elements: Element): Counter {
    return Counter(engine, elements.asList())
  }
}
