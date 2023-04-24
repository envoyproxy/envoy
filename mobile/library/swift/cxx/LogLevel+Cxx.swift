@_implementationOnly import EnvoyCxxSwiftInterop

extension LogLevel {
  func toCXX() -> Envoy.Platform.LogLevel {
    switch self {
    case .trace:
      return .trace
    case .debug:
      return .debug
    case .info:
      return .info
    case .warn:
      return .warn
    case .error:
      return .error
    case .critical:
      return .critical
    case .off:
      return .off
    }
  }
}

// MARK: - Test Helpers

extension LogLevel {
  var cxxDescription: String {
    .fromCXX(Envoy.Platform.logLevelToString(self.toCXX()))
  }
}
