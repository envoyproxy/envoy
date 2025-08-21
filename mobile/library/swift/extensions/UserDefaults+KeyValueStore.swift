import Foundation

extension UserDefaults: KeyValueStore {
  public func readValue(forKey key: String) -> String? {
    self.string(forKey: key)
  }

  public func saveValue(_ value: String, toKey key: String) {
    self.set(value, forKey: key)
  }

  public func removeKey(_ key: String) {
    self.removeObject(forKey: key)
  }
}
