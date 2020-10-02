package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Gauge`.
 */
internal class GaugeImpl : Gauge {
  internal val envoyEngine: WeakReference<EnvoyEngine>
  internal val elements: List<Element>

  internal constructor(engine: EnvoyEngine, elements: List<Element>) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.elements = elements
  }

  override fun set(value: Int) {
    envoyEngine.get()?.recordGaugeSet(
      elements.joinToString(separator = ".") { it.element }, value
    )
  }

  override fun add(amount: Int) {
    envoyEngine.get()?.recordGaugeAdd(
      elements.joinToString(separator = ".") { it.element }, amount
    )
  }

  override fun sub(amount: Int) {
    envoyEngine.get()?.recordGaugeSub(
      elements.joinToString(separator = ".") { it.element }, amount
    )
  }
}
