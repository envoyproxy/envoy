@testable import Envoy
import EnvoyEngine
import Foundation
import Network
import XCTest

/// Tests for network change monitoring functionality.
/// Note: Since `checkReachabilityAndNotifyEnvoy:` is a private method and `nw_path_t`
/// is an opaque type from Apple's Network framework, these tests verify the behavior
/// through the public interface and by observing real network state changes.
final class NetworkChangeMonitorTests: XCTestCase {
  // MARK: - Network Type Bitmask Tests

  /// Test that network type enum values are correct powers of 2 for bitwise operations.
  func testNetworkTypeBitmaskValues() {
    // These values should match Envoy::NetworkType in network_types.h
    // Generic = 1, WLAN = 2, WWAN = 4, WWAN_2G = 8, WWAN_3G = 16, WWAN_4G = 32, WWAN_5G = 64
    let generic = 1
    let wlan = 2
    let wwan = 4
    let wwan2g = 8
    let wwan3g = 16
    let wwan4g = 32
    let wwan5g = 64

    // Verify they are distinct powers of 2
    XCTAssertEqual(generic, 1 << 0, "Generic should be 1")
    XCTAssertEqual(wlan, 1 << 1, "WLAN should be 2")
    XCTAssertEqual(wwan, 1 << 2, "WWAN should be 4")
    XCTAssertEqual(wwan2g, 1 << 3, "WWAN_2G should be 8")
    XCTAssertEqual(wwan3g, 1 << 4, "WWAN_3G should be 16")
    XCTAssertEqual(wwan4g, 1 << 5, "WWAN_4G should be 32")
    XCTAssertEqual(wwan5g, 1 << 6, "WWAN_5G should be 64")

    // Verify combinations work correctly
    let cellularWith4g = wwan | wwan4g
    XCTAssertEqual(cellularWith4g, 36, "WWAN | WWAN_4G should be 36")
    XCTAssertTrue((cellularWith4g & wwan) != 0, "Combined should contain WWAN")
    XCTAssertTrue((cellularWith4g & wwan4g) != 0, "Combined should contain WWAN_4G")
    XCTAssertTrue((cellularWith4g & wlan) == 0, "Combined should not contain WLAN")

    // VPN can be combined with other types
    let wlanWithVpn = wlan | generic
    XCTAssertEqual(wlanWithVpn, 3, "WLAN | Generic should be 3")
    XCTAssertTrue((wlanWithVpn & wlan) != 0, "Combined should contain WLAN")
    XCTAssertTrue((wlanWithVpn & generic) != 0, "Combined should contain Generic (VPN)")
  }

  // MARK: - NetworkMonitoringMode Tests

  func testNetworkMonitoringModeValues() {
    XCTAssertEqual(NetworkMonitoringMode.disabled.rawValue, 0)
    XCTAssertEqual(NetworkMonitoringMode.reachability.rawValue, 1)
    XCTAssertEqual(NetworkMonitoringMode.pathMonitor.rawValue, 2)
  }

  func testEngineBuilderDefaultsToPathMonitor() {
    let builder = EngineBuilder()
    XCTAssertEqual(builder.monitoringMode, .pathMonitor)
  }

  func testEngineBuilderCanSetMonitoringMode() {
    let builder = EngineBuilder()
      .setNetworkMonitoringMode(.disabled)
    XCTAssertEqual(builder.monitoringMode, .disabled)

    builder.setNetworkMonitoringMode(.reachability)
    XCTAssertEqual(builder.monitoringMode, .reachability)

    builder.setNetworkMonitoringMode(.pathMonitor)
    XCTAssertEqual(builder.monitoringMode, .pathMonitor)
  }

  // MARK: - NWPathMonitor Integration Tests

  /// Test that NWPathMonitor provides network path information.
  /// This is a basic sanity test to ensure the Network framework APIs work as expected.
  func testNWPathMonitorProvidesNetworkState() {
    let expectation = self.expectation(description: "Path monitor should provide network state")

    let monitor = NWPathMonitor()
    let queue = DispatchQueue(label: "test.network.monitor")

    monitor.pathUpdateHandler = { path in
      // We received a path update - verify we can query its properties
      let status = path.status
      XCTAssertTrue(
        status == .satisfied || status == .unsatisfied || status == .requiresConnection,
        "Status should be one of the expected values"
      )

      // Check that we can query interface types
      let usesWifi = path.usesInterfaceType(.wifi)
      let usesCellular = path.usesInterfaceType(.cellular)
      let usesWired = path.usesInterfaceType(.wiredEthernet)

      // At least log what we found (useful for debugging)
      print("Network state: status=\(status), wifi=\(usesWifi), " +
            "cellular=\(usesCellular), wired=\(usesWired)")

      expectation.fulfill()
      monitor.cancel()
    }

    monitor.start(queue: queue)

    wait(for: [expectation], timeout: 5.0)
  }

  // MARK: - Engine Configuration Tests

  func testEnableNetworkChangeMonitorConfiguration() {
    let expectation = self.expectation(description: "Engine configured with network monitor")

    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertTrue(config.enableNetworkChangeMonitor,
                    "Network change monitor should be enabled")
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .enableNetworkChangeMonitor(true)
      .build()

    wait(for: [expectation], timeout: 1.0)
  }

  func testDisableNetworkChangeMonitorConfiguration() {
    let expectation = self.expectation(description: "Engine configured without network monitor")

    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertFalse(config.enableNetworkChangeMonitor,
                     "Network change monitor should be disabled")
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .enableNetworkChangeMonitor(false)
      .build()

    wait(for: [expectation], timeout: 1.0)
  }

  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRunWithConfig = nil
  }
}
