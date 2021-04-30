package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Gauge`.
 */
internal class GaugeImpl : Gauge {
  var envoyEngine: WeakReference<EnvoyEngine>
  var series: String
  var tags: Tags

  internal constructor(engine: EnvoyEngine, elements: List<Element>, tags: Tags = TagsBuilder().build()) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.series = elements.joinToString(separator = ".") { it.value }
    this.tags = tags
  }

  override fun set(value: Int) {
    envoyEngine.get()?.recordGaugeSet(series, this.tags.allTags(), value)
  }

  override fun set(tags: Tags, value: Int) {
    envoyEngine.get()?.recordGaugeSet(series, tags.allTags(), value)
  }

  override fun add(amount: Int) {
    envoyEngine.get()?.recordGaugeAdd(series, this.tags.allTags(), amount)
  }

  override fun add(tags: Tags, amount: Int) {
    envoyEngine.get()?.recordGaugeAdd(series, tags.allTags(), amount)
  }

  override fun sub(amount: Int) {
    envoyEngine.get()?.recordGaugeSub(series, this.tags.allTags(), amount)
  }

  override fun sub(tags: Tags, amount: Int) {
    envoyEngine.get()?.recordGaugeSub(series, tags.allTags(), amount)
  }
}
