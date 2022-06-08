@_implementationOnly import EnvoyEngine
import Foundation

/// `KeyValueStore` is an interface that may be implemented to provide access to an arbitrary
/// key-value store implementation that may be made accessible to native Envoy Mobile code.
public protocol KeyValueStore {
  /// Read a value from the key value store implementation.
  func readValue(forKey key: String) -> String?

  /// Save a value to the key value store implementation.
  func saveValue(_ value: String, toKey key: String)

  /// Remove a value from the key value store implementation.
  func removeKey(_ key: String)
}

/// KeyValueStoreImpl is an internal type used for mapping calls from the common library layer.
internal class KeyValueStoreImpl: EnvoyKeyValueStore {
  internal let implementation: KeyValueStore

  init(implementation: KeyValueStore) {
    self.implementation = implementation
  }

  func readValue(forKey key: String) -> String? {
    return implementation.readValue(forKey: key)
  }

  func saveValue(_ value: String, toKey key: String) {
    implementation.saveValue(value, toKey: key)
  }

  func removeKey(_ key: String) {
    implementation.removeKey(key)
  }
}
