package io.envoyproxy.envoymobile

/** A time-series distribution of duration measurements. */
interface Timer {

  /** Records a new duration to add to the timer. */
  fun completeWithDuration(durationMs: Int)

  /** Records a new duration to add to the timer along with tags. */
  fun completeWithDuration(tags: Tags = TagsBuilder().build(), durationMs: Int)
}
