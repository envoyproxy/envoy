@_implementationOnly import EnvoyCxxSwiftInterop
import Foundation

// swiftlint:disable force_unwrapping

extension KeyValueStore {
  func register(withName name: String) {
    let pointer = UnsafeMutablePointer<envoy_kv_store>.allocate(capacity: 1)
    pointer.pointee.save = { key, value, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let store = PointerBox<KeyValueStore>.unretained(from: context!)
        let stringKey = String.fromEnvoyData(key)!
        let stringValue = String.fromEnvoyData(value)!
        store.saveValue(stringValue, toKey: stringKey)
      }
    }

    pointer.pointee.read = { key, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let store = PointerBox<KeyValueStore>.unretained(from: context!)
        let stringKey = String.fromEnvoyData(key)!
        guard let stringValue = store.readValue(forKey: stringKey) else {
          return envoy_nodata
        }

        return stringValue.toEnvoyData()
      }
    }

    pointer.pointee.remove = { key, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let store = PointerBox<KeyValueStore>.unretained(from: context!)
        let stringKey = String.fromEnvoyData(key)!
        store.removeKey(stringKey)
      }
    }

    pointer.pointee.context = PointerBox(value: self).retainedPointer()
    register_platform_api(name, pointer)
  }
}
