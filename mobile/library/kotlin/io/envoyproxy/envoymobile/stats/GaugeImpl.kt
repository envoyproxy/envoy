package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Gauge`.
 */
internal class GaugeImpl : Gauge {
  internal val envoyEngine: WeakReference<EnvoyEngine>
  internal val series: String

  internal constructor(engine: EnvoyEngine, elements: List<Element>) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.series = elements.joinToString(separator = ".") { it.value }
  }

  override fun set(value: Int) {
    envoyEngine.get()?.recordGaugeSet(series, value)
  }

  override fun add(amount: Int) {
    envoyEngine.get()?.recordGaugeAdd(series, amount)
  }

  override fun sub(amount: Int) {
    envoyEngine.get()?.recordGaugeSub(series, amount)
  }
}
