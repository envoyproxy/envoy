package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks

/**
 * Mock implementation of `EnvoyEngine`. Used internally for testing the bridging layer & mocking.
 */
internal class MockEnvoyEngine : EnvoyEngine {
  override fun runWithConfig(envoyConfiguration: EnvoyConfiguration?, logLevel: String?): Int = 0

  override fun runWithConfig(configurationYAML: String?, logLevel: String?): Int = 0

  override fun startStream(callbacks: EnvoyHTTPCallbacks?): EnvoyHTTPStream = MockEnvoyHTTPStream(callbacks!!)

  override fun recordCounter(elements: String, count: Int) {}
}
