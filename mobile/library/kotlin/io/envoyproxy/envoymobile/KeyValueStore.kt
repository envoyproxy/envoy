package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore

/**
 * `KeyValueStore` is bridged through to `EnvoyKeyValueStore` to communicate with the engine.
 */
class KeyValueStore constructor (
  val read: ((key: String) -> String?),
  val remove: ((key: String) -> Unit),
  val save: ((key: String, value: String) -> Unit)
)

/**
 * Class responsible for bridging between the platform-level `KeyValueStore` and the
 * engine's `EnvoyKeyValueStore`.
 */
internal class EnvoyKeyValueStoreAdapter(
  private val callbacks: KeyValueStore
) : EnvoyKeyValueStore {
  override fun read(key: String): String? {
    return callbacks.read(key)
  }

  override fun remove(key: String) {
    callbacks.remove(key)
  }

  override fun save(key: String, value: String) {
    callbacks.save(key, value)
  }
}
