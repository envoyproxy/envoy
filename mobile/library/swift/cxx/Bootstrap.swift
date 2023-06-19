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
  /// - returns: A generated bootstrap object.
  func generateBootstrap() -> Bootstrap {
    Bootstrap(pointer: Envoy.CxxSwift.generateBootstrapPtr(self))
  }
}
