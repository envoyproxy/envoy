@testable import Envoy
import EnvoyEngine
import Foundation
import XCTest

// swiftlint:disable type_body_length

private struct TestFilter: Filter {}

final class EngineBuilderTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRunWithConfig = nil
    MockEnvoyEngine.onRunWithYAML = nil
  }

  func testSetRuntimeGuard() {
    let bootstrapDebugDescription = EngineBuilder()
      .setRuntimeGuard("test_feature_false", true)
      .setRuntimeGuard("test_feature_true", false)
      .bootstrapDebugDescription()
    XCTAssertTrue(
      bootstrapDebugDescription.contains(#""test_feature_false" value { bool_value: true }"#)
    )
    XCTAssertTrue(
      bootstrapDebugDescription.contains(#""test_feature_true" value { bool_value: false }"#)
    )
  }

  func testMonitoringModeDefaultsToPathMonitor() {
    let builder = EngineBuilder()
    XCTAssertEqual(builder.monitoringMode, .pathMonitor)
  }

  func testMonitoringModeSetsToValue() {
    let builder = EngineBuilder()
      .setNetworkMonitoringMode(.disabled)
    XCTAssertEqual(builder.monitoringMode, .disabled)
    builder.setNetworkMonitoringMode(.reachability)
    XCTAssertEqual(builder.monitoringMode, .reachability)
  }

  func testCustomConfigYAMLUsesSpecifiedYAMLWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithYAML = { yaml, _, _ in
      XCTAssertEqual("foobar", yaml)
      expectation.fulfill()
    }

    _ = EngineBuilder(yaml: "foobar")
      .addEngineType(MockEnvoyEngine.self)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingLogLevelAddsLogLevelWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { _, logLevel in
      XCTAssertEqual("trace", logLevel)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addLogLevel(.trace)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testEnablingInterfaceBindingAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with enabled interface binding")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertTrue(config.enableInterfaceBinding)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .enableInterfaceBinding(true)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testEnforcingTrustChainVerificationAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with enforced cert verification")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertTrue(config.enforceTrustChainVerification)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .enforceTrustChainVerification(true)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testForceIPv6AddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with force IPv6")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertTrue(config.forceIPv6)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .forceIPv6(true)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingConnectTimeoutSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(12345, config.connectTimeoutSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addConnectTimeoutSeconds(12345)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingDNSRefreshSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(23, config.dnsRefreshSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSRefreshSeconds(23)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingDNSMinRefreshSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(23, config.dnsMinRefreshSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSMinRefreshSeconds(23)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingDNSQueryTimeoutSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(234, config.dnsQueryTimeoutSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSQueryTimeoutSeconds(234)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingDNSFailureRefreshSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(1234, config.dnsFailureRefreshSecondsBase)
      XCTAssertEqual(5678, config.dnsFailureRefreshSecondsMax)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSFailureRefreshSeconds(base: 1234, max: 5678)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingH2ConnectionKeepaliveIdleIntervalMSAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(234, config.h2ConnectionKeepaliveIdleIntervalMilliseconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addH2ConnectionKeepaliveIdleIntervalMilliseconds(234)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingH2ConnectionKeepaliveTimeoutSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(234, config.h2ConnectionKeepaliveTimeoutSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addH2ConnectionKeepaliveTimeoutSeconds(234)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testSettingMaxConnectionsPerHostAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(23, config.maxConnectionsPerHost)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .setMaxConnectionsPerHost(23)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingPlatformFiltersToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(1, config.httpPlatformFilterFactories.count)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addPlatformFilter(TestFilter.init)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingStreamIdleTimeoutSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(42, config.streamIdleTimeoutSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addStreamIdleTimeoutSeconds(42)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingPerTryIdleTimeoutSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(21, config.perTryIdleTimeoutSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addPerTryIdleTimeoutSeconds(21)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingAppVersionAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("v1.2.3", config.appVersion)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addAppVersion("v1.2.3")
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingAppIdAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("com.envoymobile.ios", config.appId)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addAppId("com.envoymobile.ios")
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingNativeFiltersToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(1, config.nativeFilterChain.count)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addNativeFilter(
        name: "envoy.filters.http.buffer",
        typedConfig: """
          {
            "@type": "type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer",
            "max_request_bytes": 5242880
          }
          """
      )
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingStringAccessorToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("hello", config.stringAccessors["name"]?.getEnvoyString())
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addStringAccessor(name: "name", accessor: { "hello" })
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

#if ENVOY_MOBILE_XDS
  func testAddingRtdsConfigurationWhenRunningEnvoy() {
    let xdsBuilder = XdsBuilder(xdsServerAddress: "FAKE_SWIFT_ADDRESS", xdsServerPort: 0)
      .addRuntimeDiscoveryService(resourceName: "some_rtds_resource", timeoutInSeconds: 14325)
    let bootstrapDebugDescription = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .setXds(xdsBuilder)
      .bootstrapDebugDescription()
    XCTAssertTrue(bootstrapDebugDescription.contains("some_rtds_resource"))
    XCTAssertTrue(bootstrapDebugDescription.contains("initial_fetch_timeout { seconds: 14325 }"))
    XCTAssertTrue(bootstrapDebugDescription.contains("FAKE_SWIFT_ADDRESS"))
  }

  func testAddingCdsConfigurationWhenRunningEnvoy() {
    let xdsBuilder = XdsBuilder(xdsServerAddress: "FAKE_SWIFT_ADDRESS", xdsServerPort: 0)
      .addClusterDiscoveryService(cdsResourcesLocator: "FAKE_CDS_LOCATOR", timeoutInSeconds: 2543)
    let bootstrapDebugDescription = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .setXds(xdsBuilder)
      .bootstrapDebugDescription()
    XCTAssertTrue(bootstrapDebugDescription.contains("FAKE_CDS_LOCATOR"))
    XCTAssertTrue(bootstrapDebugDescription.contains("initial_fetch_timeout { seconds: 2543 }"))
    XCTAssertTrue(bootstrapDebugDescription.contains("FAKE_SWIFT_ADDRESS"))
  }

  func testAddingDefaultCdsConfigurationWhenRunningEnvoy() {
    let xdsBuilder = XdsBuilder(xdsServerAddress: "FAKE_SWIFT_ADDRESS", xdsServerPort: 0)
      .addClusterDiscoveryService()
    let bootstrapDebugDescription = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .setXds(xdsBuilder)
      .bootstrapDebugDescription()
    XCTAssertTrue(bootstrapDebugDescription.contains("cds_config {"))
    XCTAssertTrue(bootstrapDebugDescription.contains("initial_fetch_timeout { seconds: 5 }"))
  }

  func testAddingXdsSecurityConfigurationWhenRunningEnvoy() {
    let xdsBuilder = XdsBuilder(xdsServerAddress: "FAKE_SWIFT_ADDRESS", xdsServerPort: 0)
      .addInitialStreamHeader(header: "x-goog-api-key", value: "A1B2C3")
      .addInitialStreamHeader(header: "x-android-package", value: "com.google.myapp")
      .setSslRootCerts(rootCerts: "fake_ssl_root_certs")
      .addRuntimeDiscoveryService(resourceName: "some_rtds_resource", timeoutInSeconds: 14325)
    let bootstrapDebugDescription = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .setXds(xdsBuilder)
      .bootstrapDebugDescription()
    XCTAssertTrue(bootstrapDebugDescription.contains("x-goog-api-key"))
    XCTAssertTrue(bootstrapDebugDescription.contains("A1B2C3"))
    XCTAssertTrue(bootstrapDebugDescription.contains("x-android-package"))
    XCTAssertTrue(bootstrapDebugDescription.contains("com.google.myapp"))
    XCTAssertTrue(bootstrapDebugDescription.contains("fake_ssl_root_certs"))
  }
#endif

  func testXDSDefaultValues() {
    // rtds, ads, node_id, node_locality
    let bootstrapDebugDescription = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .bootstrapDebugDescription()
    XCTAssertFalse(bootstrapDebugDescription.contains("rtds_layer {"))
    XCTAssertFalse(bootstrapDebugDescription.contains("cds_config {"))
    XCTAssertFalse(bootstrapDebugDescription.contains("ads_config {"))
    XCTAssertTrue(bootstrapDebugDescription.contains(#"id: "envoy-mobile""#))
    XCTAssertFalse(bootstrapDebugDescription.contains("locality {"))
  }

  func testCustomNodeID() {
    let bootstrapDebugDescription = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .setNodeID("SWIFT_TEST_NODE_ID")
      .bootstrapDebugDescription()
    XCTAssertTrue(bootstrapDebugDescription.contains(#"id: "SWIFT_TEST_NODE_ID""#))
  }

  func testCustomNodeLocality() {
    let bootstrapDebugDescription = EngineBuilder()
      .setNodeLocality(region: "SWIFT_REGION", zone: "SWIFT_ZONE", subZone: "SWIFT_SUB")
      .bootstrapDebugDescription()
    XCTAssertTrue(bootstrapDebugDescription.contains(#"region: "SWIFT_REGION""#))
    XCTAssertTrue(bootstrapDebugDescription.contains(#"zone: "SWIFT_ZONE""#))
    XCTAssertTrue(bootstrapDebugDescription.contains(#"sub_zone: "SWIFT_SUB""#))
  }

  func testAddingKeyValueStoreToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("bar", config.keyValueStores["name"]?.readValue(forKey: "foo"))
      expectation.fulfill()
    }

    let testStore: KeyValueStore = {
      class TestStore: KeyValueStore {
        private var dict: [String: String] = [:]

        func readValue(forKey key: String) -> String? {
          return dict[key]
        }

        func saveValue(_ value: String, toKey key: String) {
          dict[key] = value
        }

        func removeKey(_ key: String) {
          dict[key] = nil
        }
      }

      return TestStore()
    }()

    testStore.saveValue("bar", toKey: "foo")

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addKeyValueStore(name: "name", keyValueStore: testStore)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }
}
