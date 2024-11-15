import Foundation

/// Available logging levels for an Envoy instance.
/// Note that some levels may be compiled out.
@objc
public enum LogLevel: Int {
  case trace = 0
  case debug = 1
  case info = 2
  case warn = 3
  case error = 4
  case critical = 5
  case off = -1

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
