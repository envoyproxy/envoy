package io.envoyproxy.envoymobile

/** A time series gauge. */
interface Gauge {

  /** Set the gauge by the given value. */
  fun set(value: Int)

  /** Add the given amount to the gauge. */
  fun add(amount: Int)

  /** Subtract the given amount from the gauge. */
  fun sub(amount: Int)
}
