@testable import Envoy
import EnvoyEngine
import Foundation
import XCTest

// swiftlint:disable type_body_length

private let kMockTemplate =
"""
fixture_template:
- name: mock
  clusters:
#{custom_clusters}
  listeners:
#{custom_listeners}
  filters:
#{custom_filters}
  runtime:
#{custom_runtime}
  routes:
#{custom_routes}
"""

private struct TestFilter: Filter {}

final class EngineBuilderTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRunWithConfig = nil
    MockEnvoyEngine.onRunWithYAML = nil
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

  func testAdminInterfaceIsDisabledByDefault() {
    let expectation = self.expectation(description: "Run called with disabled admin interface")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertFalse(config.adminInterfaceEnabled)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

#if ENVOY_ADMIN_FUNCTIONALITY
  func testEnablingAdminInterfaceAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with enabled admin interface")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertTrue(config.adminInterfaceEnabled)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .enableAdminInterface()
      .build()
    self.waitForExpectations(timeout: 0.01)
  }
#endif

  func testEnablingHappyEyeballsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with enabled happy eyeballs")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertTrue(config.enableHappyEyeballs)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .enableHappyEyeballs(true)
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

  func testAddinggrpcStatsDomainAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("stats.envoyproxy.io", config.grpcStatsDomain)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addGrpcStatsDomain("stats.envoyproxy.io")
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

  func testAddingStatsFlushSecondsAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(42, config.statsFlushSeconds)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addStatsFlushSeconds(42)
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

  func testAddingVirtualClustersAddsToConfigurationWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(["test"], config.virtualClusters)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addVirtualClusters(["test"])
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
      .addNativeFilter(name: "test_name", typedConfig: "config")
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
