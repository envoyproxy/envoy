package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine

/**
 * Envoy implementation of `PulseClient`.
 */
internal class PulseClientImpl constructor(
  internal val engine: EnvoyEngine
) : PulseClient {

  override fun counter(vararg elements: Element): Counter {
    return CounterImpl(engine, elements.asList())
  }

  override fun counter(vararg elements: Element, tags: Tags): Counter {
    return CounterImpl(engine, elements.asList(), tags)
  }

  override fun gauge(vararg elements: Element): Gauge {
    return GaugeImpl(engine, elements.asList())
  }

  override fun gauge(vararg elements: Element, tags: Tags): Gauge {
    return GaugeImpl(engine, elements.asList(), tags)
  }

  override fun timer(vararg elements: Element): Timer {
    return TimerImpl(engine, elements.asList())
  }

  override fun timer(vararg elements: Element, tags: Tags): Timer {
    return TimerImpl(engine, elements.asList(), tags)
  }

  override fun distribution(vararg elements: Element): Distribution {
    return DistributionImpl(engine, elements.asList())
  }

  override fun distribution(vararg elements: Element, tags: Tags): Distribution {
    return DistributionImpl(engine, elements.asList(), tags)
  }

  override fun log(level: LogLevel, message: String, logTags: Map<String, String>?) {
    // TODO: currently does nothing, it will be implemented soon
  }
}
