package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Distribution` for measurements of quantile data for int values
 */
internal class DistributionImpl : Distribution {
  internal val envoyEngine: WeakReference<EnvoyEngine>
  internal val elements: String

  internal constructor(engine: EnvoyEngine, elements: List<Element>) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.elements = elements.joinToString(separator = ".") { it.value }
  }

  override fun recordValue(value: Int) {
    envoyEngine.get()?.recordHistogramValue(elements, value)
  }
}
