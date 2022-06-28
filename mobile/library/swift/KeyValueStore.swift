@_implementationOnly import EnvoyEngine
import Foundation

/// `KeyValueStore` is an interface that may be implemented to provide access to an arbitrary
/// key-value store implementation that may be made accessible to native Envoy Mobile code.
public protocol KeyValueStore {
  /// Read a value from the key value store implementation.
  ///
  /// - parameter key: The key whose value should be read.
  ///
  /// - returns: The value read for the key if it was found.
  func readValue(forKey key: String) -> String?

  /// Save a value to the key value store implementation.
  ///
  /// - parameter value: The value to save.
  /// - parameter key:   The key to associate with the saved value.
  func saveValue(_ value: String, toKey key: String)

  /// Remove a value from the key value store implementation.
  ///
  /// - parameter key: The key whose value should be removed.
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
