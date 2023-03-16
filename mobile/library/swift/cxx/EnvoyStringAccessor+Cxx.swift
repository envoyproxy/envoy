@_implementationOnly import EnvoyCxxSwiftInterop
@_implementationOnly import EnvoyEngine
import Foundation

// swiftlint:disable force_cast force_unwrapping

extension EnvoyStringAccessor {
  func register(withName name: String) {
    let pointer = UnsafeMutablePointer<envoy_string_accessor>.allocate(capacity: 1)
    pointer.pointee.get_string = { context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let accessor = PointerBox<EnvoyStringAccessor>.unretained(from: context!)
        let string = accessor.getEnvoyString()
        return string.toEnvoyData()
      }
    }

    pointer.pointee.context = PointerBox(value: self).retainedPointer()
    register_platform_api(name, pointer)
  }
}
