@_implementationOnly import EnvoyCxxSwiftInterop
@_implementationOnly import EnvoyEngine

// swiftlint:disable force_cast force_unwrapping

extension EnvoyStringAccessor {
  func register(withName name: String) {
    let pointer = UnsafeMutablePointer<envoy_string_accessor>.allocate(capacity: 1)
    pointer.pointee.get_string = { context in
      let accessor = PointerBox<EnvoyStringAccessor>.unretained(from: context!)
      let string = accessor.getEnvoyString()
      return string.toEnvoyData()
    }

    pointer.pointee.context = PointerBox(value: self).retainedPointer()
    register_platform_api(name, pointer)
  }
}
