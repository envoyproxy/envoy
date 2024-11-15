package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore

/**
 * `KeyValueStore` is an interface that may be implemented to provide access to an arbitrary
 * key-value store implementation that may be made accessible to native Envoy Mobile code.
 */
interface KeyValueStore : EnvoyKeyValueStore
