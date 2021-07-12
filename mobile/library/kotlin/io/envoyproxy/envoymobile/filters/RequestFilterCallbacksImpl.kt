package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterCallbacks

/**
 * Envoy implementation of `RequestFilterCallbacks`.
 */
internal class RequestFilterCallbacksImpl constructor(
  internal val callbacks: EnvoyHTTPFilterCallbacks
) : RequestFilterCallbacks {

  override fun resumeRequest() {
    callbacks.resumeIteration()
  }

  override fun resetIdleTimer() {
    callbacks.resetIdleTimer()
  }
}
