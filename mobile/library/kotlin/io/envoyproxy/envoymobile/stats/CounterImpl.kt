package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Counter`.
 */
internal class CounterImpl : Counter {
  internal val envoyEngine: WeakReference<EnvoyEngine>
  private val series: String

  internal constructor(engine: EnvoyEngine, elements: List<Element>) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.series = elements.joinToString(separator = ".") { it.value }
  }

  // TODO: potentially raise error to platform if the operation is not successful.
  override fun increment(count: Int) {
    envoyEngine.get()?.recordCounterInc(series, emptyMap<String, String>(), count)
  }
}
