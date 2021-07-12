package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterCallbacks

/**
 * Envoy implementation of `ResponseFilterCallbacks`.
 */
internal class ResponseFilterCallbacksImpl constructor(
  internal val callbacks: EnvoyHTTPFilterCallbacks
) : ResponseFilterCallbacks {

  override fun resumeResponse() {
    callbacks.resumeIteration()
  }

  override fun resetIdleTimer() {
    callbacks.resetIdleTimer()
  }
}
