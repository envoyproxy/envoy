@_implementationOnly import EnvoyCxxSwiftInterop

/// Wrapper around Envoy's bootstrap configuration.
final class Bootstrap {
  let ptr: Envoy.CxxSwift.BootstrapPtr
  init(ptr: Envoy.CxxSwift.BootstrapPtr) {
    self.ptr = ptr
  }
}

extension Envoy.Platform.EngineBuilder {
  func generateBootstrap() -> Bootstrap {
    Bootstrap(ptr: Envoy.CxxSwift.generateBootstrapPtr(self))
  }
}
