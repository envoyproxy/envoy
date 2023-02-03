@testable import Envoy
import EnvoyEngine
import Foundation
import XCTest

// swiftlint:disable file_length type_body_length

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
  routes:
#{custom_routes}
"""

private struct TestFilter: Filter {}

final class EngineBuilderTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRunWithConfig = nil
    MockEnvoyEngine.onRunWithTemplate = nil
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

  func testCustomConfigTemplateUsesSpecifiedYAMLWhenRunningEnvoy() {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithTemplate = { yaml, _, _ in
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
      XCTAssertEqual("[test]", config.virtualClusters)
      expectation.fulfill()
    }

    _ = EngineBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addVirtualClusters("[test]")
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

  func testResolvesYAMLWithIndividuallySetValues() throws {
    let config = EnvoyConfiguration(
      adminInterfaceEnabled: false,
      grpcStatsDomain: "stats.envoyproxy.io",
      connectTimeoutSeconds: 200,
      dnsRefreshSeconds: 300,
      dnsFailureRefreshSecondsBase: 400,
      dnsFailureRefreshSecondsMax: 500,
      dnsQueryTimeoutSeconds: 800,
      dnsMinRefreshSeconds: 100,
      dnsPreresolveHostnames: "[test]",
      enableDNSCache: false,
      dnsCacheSaveIntervalSeconds: 0,
      enableHappyEyeballs: true,
      enableHttp3: true,
      enableGzip: true,
      enableBrotli: false,
      enableInterfaceBinding: true,
      enableDrainPostDnsRefresh: false,
      enforceTrustChainVerification: false,
      forceIPv6: false,
      enablePlatformCertificateValidation: false,
      h2ConnectionKeepaliveIdleIntervalMilliseconds: 1,
      h2ConnectionKeepaliveTimeoutSeconds: 333,
      maxConnectionsPerHost: 100,
      statsFlushSeconds: 600,
      streamIdleTimeoutSeconds: 700,
      perTryIdleTimeoutSeconds: 777,
      appVersion: "v1.2.3",
      appId: "com.envoymobile.ios",
      virtualClusters: "[test]",
      directResponseMatchers: "",
      directResponses: "",
      nativeFilterChain: [
        EnvoyNativeFilterConfig(name: "filter_name", typedConfig: "test_config"),
      ],
      platformFilterChain: [
        EnvoyHTTPFilterFactory(filterName: "TestFilter", factory: TestFilter.init),
      ],
      stringAccessors: [:],
      keyValueStores: [:],
      statsSinks: []
    )
    let resolvedYAML = try XCTUnwrap(config.resolveTemplate(kMockTemplate))
    XCTAssertTrue(resolvedYAML.contains("&connect_timeout 200s"))
    XCTAssertTrue(resolvedYAML.contains("&dns_refresh_rate 300s"))
    XCTAssertTrue(resolvedYAML.contains("&dns_fail_base_interval 400s"))
    XCTAssertTrue(resolvedYAML.contains("&dns_fail_max_interval 500s"))
    XCTAssertTrue(resolvedYAML.contains("&dns_query_timeout 800s"))
    XCTAssertTrue(resolvedYAML.contains("&dns_min_refresh_rate 100s"))
    XCTAssertTrue(resolvedYAML.contains("&dns_preresolve_hostnames [test]"))
    XCTAssertTrue(resolvedYAML.contains("&dns_lookup_family ALL"))
    XCTAssertFalse(resolvedYAML.contains("&persistent_dns_cache_config"))
    XCTAssertTrue(resolvedYAML.contains("&enable_interface_binding true"))
    XCTAssertTrue(resolvedYAML.contains("&trust_chain_verification ACCEPT_UNTRUSTED"))
    XCTAssertTrue(resolvedYAML.contains("""
&validation_context
  trusted_ca:
    inline_string: *tls_root_certs
"""
        ))
    XCTAssertTrue(resolvedYAML.contains("&enable_drain_post_dns_refresh false"))

    // HTTP/2
    XCTAssertTrue(resolvedYAML.contains("&h2_connection_keepalive_idle_interval 0.001s"))
    XCTAssertTrue(resolvedYAML.contains("&h2_connection_keepalive_timeout 333s"))

    XCTAssertTrue(resolvedYAML.contains("&max_connections_per_host 100"))

    XCTAssertTrue(resolvedYAML.contains("&stream_idle_timeout 700s"))
    XCTAssertTrue(resolvedYAML.contains("&per_try_idle_timeout 777s"))

    XCTAssertFalse(resolvedYAML.contains("admin: *admin_interface"))

    // Decompression
    XCTAssertTrue(resolvedYAML.contains("decompressor.v3.Gzip"))
    XCTAssertFalse(resolvedYAML.contains("decompressor.v3.Brotli"))

    // Metadata
    XCTAssertTrue(resolvedYAML.contains("device_os: iOS"))
    XCTAssertTrue(resolvedYAML.contains("app_version: v1.2.3"))
    XCTAssertTrue(resolvedYAML.contains("app_id: com.envoymobile.ios"))

    XCTAssertTrue(resolvedYAML.contains("&virtual_clusters [test]"))

    // Stats
    XCTAssertTrue(resolvedYAML.contains("&stats_domain stats.envoyproxy.io"))
    XCTAssertTrue(resolvedYAML.contains("&stats_flush_interval 600s"))

    // Filters
    XCTAssertTrue(resolvedYAML.contains("filter_name: TestFilter"))
    XCTAssertTrue(resolvedYAML.contains("name: filter_name"))
    XCTAssertTrue(resolvedYAML.contains("typed_config: test_config"))
  }

  func testResolvesYAMLWithAlternateValues() throws {
    let config = EnvoyConfiguration(
      adminInterfaceEnabled: false,
      grpcStatsDomain: "stats.envoyproxy.io",
      connectTimeoutSeconds: 200,
      dnsRefreshSeconds: 300,
      dnsFailureRefreshSecondsBase: 400,
      dnsFailureRefreshSecondsMax: 500,
      dnsQueryTimeoutSeconds: 800,
      dnsMinRefreshSeconds: 100,
      dnsPreresolveHostnames: "[test]",
      enableDNSCache: true,
      dnsCacheSaveIntervalSeconds: 10,
      enableHappyEyeballs: false,
      enableHttp3: false,
      enableGzip: false,
      enableBrotli: true,
      enableInterfaceBinding: false,
      enableDrainPostDnsRefresh: true,
      enforceTrustChainVerification: true,
      forceIPv6: false,
      enablePlatformCertificateValidation: true,
      h2ConnectionKeepaliveIdleIntervalMilliseconds: 1,
      h2ConnectionKeepaliveTimeoutSeconds: 333,
      maxConnectionsPerHost: 100,
      statsFlushSeconds: 600,
      streamIdleTimeoutSeconds: 700,
      perTryIdleTimeoutSeconds: 777,
      appVersion: "v1.2.3",
      appId: "com.envoymobile.ios",
      virtualClusters: "[test]",
      directResponseMatchers: "",
      directResponses: "",
      nativeFilterChain: [
        EnvoyNativeFilterConfig(name: "filter_name", typedConfig: "test_config"),
      ],
      platformFilterChain: [
        EnvoyHTTPFilterFactory(filterName: "TestFilter", factory: TestFilter.init),
      ],
      stringAccessors: [:],
      keyValueStores: [:],
      statsSinks: []
    )
    let resolvedYAML = try XCTUnwrap(config.resolveTemplate(kMockTemplate))
    XCTAssertTrue(resolvedYAML.contains("&dns_lookup_family V4_PREFERRED"))
// swiftlint:disable line_length
    XCTAssertTrue(resolvedYAML.contains(
"""
- &persistent_dns_cache_config
  config:
    name: "envoy.key_value.platform"
    typed_config:
      "@type": type.googleapis.com/envoymobile.extensions.key_value.platform.PlatformKeyValueStoreConfig
      key: dns_persistent_cache
      save_interval:
        seconds: *persistent_dns_cache_save_interval
      max_entries: 100
"""
    ))
    XCTAssertTrue(resolvedYAML.contains("&persistent_dns_cache_save_interval 10"))
// swiftlint:enable line_length
    XCTAssertTrue(resolvedYAML.contains("&enable_interface_binding false"))
    XCTAssertTrue(resolvedYAML.contains("&trust_chain_verification VERIFY_TRUST_CHAIN"))
    XCTAssertTrue(resolvedYAML.contains(
"""
&validation_context
  custom_validator_config:
    name: "envoy_mobile.cert_validator.platform_bridge_cert_validator"
"""
    ))
    XCTAssertTrue(resolvedYAML.contains("&enable_drain_post_dns_refresh true"))

    // Decompression
    XCTAssertFalse(resolvedYAML.contains("decompressor.v3.Gzip"))
    XCTAssertTrue(resolvedYAML.contains("decompressor.v3.Brotli"))
  }

  func testReturnsNilWhenUnresolvedValueInTemplate() {
    let config = EnvoyConfiguration(
      adminInterfaceEnabled: true,
      grpcStatsDomain: "stats.envoyproxy.io",
      connectTimeoutSeconds: 200,
      dnsRefreshSeconds: 300,
      dnsFailureRefreshSecondsBase: 400,
      dnsFailureRefreshSecondsMax: 500,
      dnsQueryTimeoutSeconds: 800,
      dnsMinRefreshSeconds: 100,
      dnsPreresolveHostnames: "[test]",
      enableDNSCache: false,
      dnsCacheSaveIntervalSeconds: 0,
      enableHappyEyeballs: false,
      enableHttp3: false,
      enableGzip: false,
      enableBrotli: false,
      enableInterfaceBinding: false,
      enableDrainPostDnsRefresh: false,
      enforceTrustChainVerification: true,
      forceIPv6: false,
      enablePlatformCertificateValidation: true,
      h2ConnectionKeepaliveIdleIntervalMilliseconds: 222,
      h2ConnectionKeepaliveTimeoutSeconds: 333,
      maxConnectionsPerHost: 100,
      statsFlushSeconds: 600,
      streamIdleTimeoutSeconds: 700,
      perTryIdleTimeoutSeconds: 700,
      appVersion: "v1.2.3",
      appId: "com.envoymobile.ios",
      virtualClusters: "[test]",
      directResponseMatchers: "",
      directResponses: "",
      nativeFilterChain: [],
      platformFilterChain: [],
      stringAccessors: [:],
      keyValueStores: [:],
      statsSinks: []
    )
    XCTAssertNil(config.resolveTemplate("{{ missing }}"))
  }
}
