package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.lang.ref.WeakReference

/**
 * Envoy implementation of a `Timer` for time measurements e.g. distribution of durations.
 */
internal class TimerImpl : Timer {
  var envoyEngine: WeakReference<EnvoyEngine>
  var series: String
  var tags: Tags

  internal constructor(engine: EnvoyEngine, elements: List<Element>, tags: Tags = TagsBuilder().build()) {
    this.envoyEngine = WeakReference<EnvoyEngine>(engine)
    this.series = elements.joinToString(separator = ".") { it.value }
    this.tags = tags
  }

  override fun recordDuration(durationMs: Int) {
    envoyEngine.get()?.recordHistogramDuration(series, this.tags.allTags(), durationMs)
  }

  override fun recordDuration(tags: Tags, durationMs: Int) {
    envoyEngine.get()?.recordHistogramDuration(series, tags.allTags(), durationMs)
  }
}
