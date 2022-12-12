package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Distribution` for measurements of quantile data for int values
 */
internal class DistributionImpl : Distribution {
  var envoyEngine: WeakReference<EnvoyEngine>
  var series: String
  var tags: Tags

  internal constructor(engine: EnvoyEngine, elements: List<Element>, tags: Tags = TagsBuilder().build()) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.series = elements.joinToString(separator = ".") { it.value }
    this.tags = tags
  }

  override fun recordValue(value: Int) {
    envoyEngine.get()?.recordHistogramValue(series, this.tags.allTags(), value)
  }

  override fun recordValue(tags: Tags, value: Int) {
    envoyEngine.get()?.recordHistogramValue(series, tags.allTags(), value)
  }
}
