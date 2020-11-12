package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning

/**
 * Mock implementation of `EnvoyEngine`. Used internally for testing the bridging layer & mocking.
 */
internal class MockEnvoyEngine : EnvoyEngine {
  override fun runWithConfig(envoyConfiguration: EnvoyConfiguration?, logLevel: String?, onEngineRunning: EnvoyOnEngineRunning): Int = 0

  override fun runWithConfig(configurationYAML: String?, logLevel: String?, onEngineRunning: EnvoyOnEngineRunning): Int = 0

  override fun startStream(callbacks: EnvoyHTTPCallbacks?): EnvoyHTTPStream = MockEnvoyHTTPStream(callbacks!!)

  override fun recordCounterInc(elements: String, count: Int): Int = 0

  override fun recordGaugeSet(elements: String, value: Int): Int = 0

  override fun recordGaugeAdd(elements: String, amount: Int): Int = 0

  override fun recordGaugeSub(elements: String, amount: Int): Int = 0
}
