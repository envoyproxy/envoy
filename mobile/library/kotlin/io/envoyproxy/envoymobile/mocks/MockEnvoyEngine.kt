package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor

/**
 * Mock implementation of `EnvoyEngine`. Used internally for testing the bridging layer & mocking.
 */
internal class MockEnvoyEngine : EnvoyEngine {
  override fun runWithConfig(envoyConfiguration: EnvoyConfiguration?, logLevel: String?): Int = 0

  override fun runWithTemplate(
    configurationYAML: String,
    envoyConfiguration: EnvoyConfiguration,
    logLevel: String
  ): Int = 0

  override fun startStream(callbacks: EnvoyHTTPCallbacks?): EnvoyHTTPStream = MockEnvoyHTTPStream(callbacks!!)

  override fun terminate() = Unit

  override fun recordCounterInc(elements: String, tags: MutableMap<String, String>, count: Int): Int = 0

  override fun recordGaugeSet(elements: String, tags: MutableMap<String, String>, value: Int): Int = 0

  override fun recordGaugeAdd(elements: String, tags: MutableMap<String, String>, amount: Int): Int = 0

  override fun recordGaugeSub(elements: String, tags: MutableMap<String, String>, amount: Int): Int = 0

  override fun recordHistogramDuration(elements: String, tags: MutableMap<String, String>, durationMs: Int): Int = 0

  override fun recordHistogramValue(elements: String, tags: MutableMap<String, String>, value: Int): Int = 0

  override fun registerStringAccessor(accessorName: String, accessor: EnvoyStringAccessor): Int = 0

  override fun flushStats() = Unit
}
