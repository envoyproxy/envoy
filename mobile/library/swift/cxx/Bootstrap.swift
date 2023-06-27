@_implementationOnly import EnvoyCxxSwiftInterop

/// Wrapper around Envoy's bootstrap configuration.
final class Bootstrap {
  /// The underlying pointer to the C++ bootstrap configuration object.
  let pointer: Envoy.CxxSwift.BootstrapPtr

  /// Creates a Swift `Bootstrap` object from an underlying pointer to
  /// the C++ bootstrap configuration object.
  ///
  /// - parameter pointer: The underlying pointer to the C++ bootstrap configuration object.
  init(pointer: Envoy.CxxSwift.BootstrapPtr) {
    self.pointer = pointer
  }

  /// Generates a string description of this instance for debugging purposes.
  var debugDescription: String {
    .fromCXX(Envoy.CxxSwift.bootstrapDebugDescription(self.pointer))
  }
}

extension Envoy.Platform.EngineBuilder {
  /// Overrides the generateBootstrap() virtual function in the C++ EngineBuilder.
  ///
  /// NOTE: This function is `mutating` because we pass in the EngineBuilder by reference to
  /// CxxSwift.generateBootstrapPtr(). The CxxSwift.generateBootstrapPtr() function cannot take
  /// a const reference (const&) because passing by const reference is bridged as a value type
  /// (see https://github.com/apple/swift/blob/main/docs/CppInteroperability/InteropOddities.md)
  /// and EngineBuilder can't be a value type because it has virtual functions.
  ///
  /// - returns: A generated bootstrap object.
  mutating func generateBootstrap() -> Bootstrap {
    Bootstrap(pointer: Envoy.CxxSwift.generateBootstrapPtr(&self))
  }
}
