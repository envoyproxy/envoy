package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * A time series counter.
 */
class Counter {
  private val envoyEngine: WeakReference<EnvoyEngine>
  private val elements: List<Element>

  internal constructor(engine: EnvoyEngine, elements: List<Element>) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.elements = elements
  }

  /**
   * Increment the counter by the given count.
   */
  fun increment(count: Int = 1) {
    envoyEngine.get()?.recordCounter(
      elements.joinToString(separator = ".") { it.element }, count
    )
  }
}
