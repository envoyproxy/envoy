@_implementationOnly import EnvoyCxxSwiftInterop

extension LogLevel {
  func toCXX() -> Envoy.Platform.LogLevel {
    switch self {
    case .trace:
      return Envoy.CxxSwift.LogLevelTrace
    case .debug:
      return Envoy.CxxSwift.LogLevelDebug
    case .info:
      return Envoy.CxxSwift.LogLevelInfo
    case .warn:
      return Envoy.CxxSwift.LogLevelWarn
    case .error:
      return Envoy.CxxSwift.LogLevelError
    case .critical:
      return Envoy.CxxSwift.LogLevelCritical
    case .off:
      return Envoy.CxxSwift.LogLevelOff
    }
  }
}

// MARK: - Test Helpers

extension LogLevel {
  func toCXXDescription() -> String {
    String(cString: Envoy.Platform.logLevelToString(self.toCXX()).c_str())
  }
}
