package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor

/**
 * `StringAccessor` is bridged through to `EnvoyStringAccessor` to communicate with the engine.
 */
class StringAccessor constructor (
  /**
   * Accessor for a string exposed by a platform.
   */
  val getEnvoyString: (() -> String)
)

/**
 * Class responsible for bridging between the platform-level `StringAccessor` and the
 * engine's `EnvoyStringAccessor`.
 */
internal class EnvoyStringAccessorAdapter(
  private val callbacks: StringAccessor
) : EnvoyStringAccessor {
  override fun getEnvoyString(): String {
    return callbacks.getEnvoyString()
  }
}
