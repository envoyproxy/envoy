@_implementationOnly import EnvoyCxxSwiftInterop

// swiftlint:disable force_unwrapping

extension KeyValueStore {
  func register(withName name: String) {
    let pointer = UnsafeMutablePointer<envoy_kv_store>.allocate(capacity: 1)
    pointer.pointee.save = { key, value, context in
      let store = PointerBox<KeyValueStore>.unretained(from: context!)
      let stringKey = String.fromEnvoyData(key)!
      let stringValue = String.fromEnvoyData(value)!
      store.saveValue(stringValue, toKey: stringKey)
    }

    pointer.pointee.read = { key, context in
      let store = PointerBox<KeyValueStore>.unretained(from: context!)
      let stringKey = String.fromEnvoyData(key)!
      guard let stringValue = store.readValue(forKey: stringKey) else {
        return envoy_nodata
      }

      return stringValue.toEnvoyData()
    }

    pointer.pointee.remove = { key, context in
      let store = PointerBox<KeyValueStore>.unretained(from: context!)
      let stringKey = String.fromEnvoyData(key)!
      store.removeKey(stringKey)
    }

    pointer.pointee.context = PointerBox(value: self).retainedPointer()
    register_platform_api(name, pointer)
  }
}
