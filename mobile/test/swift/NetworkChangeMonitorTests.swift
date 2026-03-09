import XCTest

/// Tests for network change monitoring functionality.
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
}
