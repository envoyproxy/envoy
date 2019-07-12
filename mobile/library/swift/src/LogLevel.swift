import Foundation

/// Available logging levels for an Envoy instance.
/// Note that some levels may be compiled out.
@objc
public enum LogLevel: Int {
  case trace
  case debug
  case info
  case warn
  case error
  case critical
  case off

  /// String representation of the log level.
  var stringValue: String {
    switch self {
    case .trace:
      return "trace"
    case .debug:
      return "debug"
    case .info:
      return "info"
    case .warn:
      return "warn"
    case .error:
      return "error"
    case .critical:
      return "critical"
    case .off:
      return "off"
    }
  }
}
