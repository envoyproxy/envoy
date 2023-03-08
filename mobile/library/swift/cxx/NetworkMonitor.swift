import Dispatch
@_implementationOnly import EnvoyCxxSwiftInterop
import Foundation
import Network

/// Monitors network changes in order to update Envoy network cluster preferences.
final class NetworkMonitor {
  private let queue = DispatchQueue(label: "io.envoyproxy.envoymobile.EnvoyNetworkMonitor",
                                    qos: .utility)
  private let pathMonitor = NWPathMonitor()

  /// Creates an instance of the monitor.
  init() {}

  /// Start monitoring reachability using `NWPathMonitor`, updating the
  /// preferred Envoy network cluster on changes.
  ///
  /// - parameter engineHandle: The handle for the engine whose preferred Envoy network cluster
  ///                           should be updated.
  func start(engineHandle: envoy_engine_t) {
    var previousNetworkType: envoy_network_t?
    self.pathMonitor.pathUpdateHandler = { path in
      guard path.status == .satisfied else {
        return
      }

      let isCellular = path.usesInterfaceType(.cellular)
      var network = ENVOY_NET_WLAN
      if !isCellular {
        let isWifi = path.usesInterfaceType(.wifi)
        network = isWifi ? ENVOY_NET_WLAN : ENVOY_NET_GENERIC
      }

      if network != previousNetworkType {
        NSLog("[Envoy] setting preferred network to \(network)")
        set_preferred_network(engineHandle, network)
        previousNetworkType = network
      }

      // TODO(jpsim): Should we shadow or otherwise compare these results with the reachability
      // flags?

      // TODO(jpsim): Should we report back other properties of the reachable path?
      //
      // - nw_path_get_status:
      // https://developer.apple.com/documentation/network/2976886-nw_path_get_status
      // - nw_path_uses_interface_type:
      // https://developer.apple.com/documentation/network/2976898-nw_path_uses_interface_type
      // - nw_path_enumerate_gateways:
      // https://developer.apple.com/documentation/network/3175017-nw_path_enumerate_gateways
      // - nw_path_has_ipv4:
      // https://developer.apple.com/documentation/network/2976888-nw_path_has_ipv4
      // - nw_path_has_ipv6:
      // https://developer.apple.com/documentation/network/2976889-nw_path_has_ipv6
      // - nw_path_has_dns:
      // https://developer.apple.com/documentation/network/2976887-nw_path_has_dns
      // - nw_path_is_constrained:
      // https://developer.apple.com/documentation/network/3131049-nw_path_is_constrained
      // - nw_path_is_expensive:
      // https://developer.apple.com/documentation/network/2976891-nw_path_is_expensive
      // - nw_path_copy_effective_remote_endpoint:
      // https://developer.apple.com/documentation/network/2976883-nw_path_copy_effective_remote_en
    }

    self.pathMonitor.start(queue: self.queue)
  }

  deinit {
    self.pathMonitor.cancel()
  }
}
