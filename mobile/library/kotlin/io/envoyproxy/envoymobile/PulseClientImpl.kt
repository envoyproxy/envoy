package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine

/** Envoy implementation of `PulseClient`. */
class PulseClientImpl(internal val engine: EnvoyEngine) : PulseClient {
  override fun counter(vararg elements: Element): Counter {
    return CounterImpl(engine, elements.asList())
  }

  override fun counter(vararg elements: Element, tags: Tags): Counter {
    return CounterImpl(engine, elements.asList(), tags)
  }
}
