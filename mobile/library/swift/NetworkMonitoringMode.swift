import Foundation

/// The different ways Envoy Mobile can monitor network reachability
/// state.
@objc
public enum NetworkMonitoringMode: Int {
  /// Do not monitor changes to the network reachability state.
  case disabled = 0
  /// Enable network monitoring (legacy compatibility value).
  case reachability = 1
  /// Enable network monitoring.
  case pathMonitor = 2
}
