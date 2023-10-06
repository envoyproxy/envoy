package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Counter`.
 */
internal class CounterImpl : Counter {
  var envoyEngine: WeakReference<EnvoyEngine>
  var series: String
  var tags: Tags

  internal constructor(engine: EnvoyEngine, elements: List<Element>, tags: Tags = TagsBuilder().build()) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.series = elements.joinToString(separator = ".") { it.value }
    this.tags = tags
  }

  // TODO: potentially raise error to platform if the operation is not successful.
  override fun increment(count: Int) {
    envoyEngine.get()?.recordCounterInc(series, this.tags.allTags(), count)
  }

  override fun increment(tags: Tags, count: Int) {
    envoyEngine.get()?.recordCounterInc(series, tags.allTags(), count)
  }
}
