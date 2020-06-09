@testable import Envoy
import EnvoyEngine
import Foundation
import XCTest

private let kMockTemplate = """
mock_template:
- name: mock
  stats_domain: {{ stats_domain }}
  device_os: {{ device_os }}
  connect_timeout: {{ connect_timeout_seconds }}s
  dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
  dns_failure_refresh_rate:
    base_interval: {{ dns_failure_refresh_rate_seconds_base }}s
    max_interval: {{ dns_failure_refresh_rate_seconds_max }}s
  stats_flush_interval: {{ stats_flush_interval_seconds }}s
  app_version: {{ app_version }}
  app_id: {{ app_id }}
  virtual_clusters: {{ virtual_clusters }}
"""

final class StreamClientBuilderTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRunWithConfig = nil
    MockEnvoyEngine.onRunWithYAML = nil
  }

  func testCustomConfigYAMLUsesSpecifiedYAMLWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithYAML = { yaml, _ in
      XCTAssertEqual("foobar", yaml)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder(yaml: "foobar")
      .addEngineType(MockEnvoyEngine.self)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingLogLevelAddsLogLevelWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { _, logLevel in
      XCTAssertEqual("trace", logLevel)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addLogLevel(.trace)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingStatsDomainAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("stats.foo.com", config.statsDomain)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addStatsDomain("stats.foo.com")
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingConnectTimeoutSecondsAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(12345, config.connectTimeoutSeconds)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addConnectTimeoutSeconds(12345)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingDNSRefreshSecondsAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(23, config.dnsRefreshSeconds)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSRefreshSeconds(23)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingDNSFailureRefreshSecondsAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(1234, config.dnsFailureRefreshSecondsBase)
      XCTAssertEqual(5678, config.dnsFailureRefreshSecondsMax)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSFailureRefreshSeconds(base: 1234, max: 5678)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingStatsFlushSecondsAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(42, config.statsFlushSeconds)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addStatsFlushSeconds(42)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingAppVersionAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("v1.2.3", config.appVersion)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addAppVersion("v1.2.3")
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingAppIdAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("com.mydomain.myapp", config.appId)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addAppId("com.mydomain.myapp")
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingVirtualClustersAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual("[test]", config.virtualClusters)
      expectation.fulfill()
    }

    _ = try StreamClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addVirtualClusters("[test]")
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testResolvesYAMLWithIndividuallySetValues() throws {
    let config = EnvoyConfiguration(statsDomain: "stats.foo.com",
                                    connectTimeoutSeconds: 200,
                                    dnsRefreshSeconds: 300,
                                    dnsFailureRefreshSecondsBase: 400,
                                    dnsFailureRefreshSecondsMax: 500,
                                    statsFlushSeconds: 600,
                                    appVersion: "v1.2.3",
                                    appId: "com.mydomain.myapp",
                                    virtualClusters: "[test]")
    let resolvedYAML = try XCTUnwrap(config.resolveTemplate(kMockTemplate))
    XCTAssertTrue(resolvedYAML.contains("stats_domain: stats.foo.com"))
    XCTAssertTrue(resolvedYAML.contains("connect_timeout: 200s"))
    XCTAssertTrue(resolvedYAML.contains("dns_refresh_rate: 300s"))
    XCTAssertTrue(resolvedYAML.contains("base_interval: 400s"))
    XCTAssertTrue(resolvedYAML.contains("max_interval: 500s"))
    XCTAssertTrue(resolvedYAML.contains("stats_flush_interval: 600s"))
    XCTAssertTrue(resolvedYAML.contains("device_os: iOS"))
    XCTAssertTrue(resolvedYAML.contains("app_version: v1.2.3"))
    XCTAssertTrue(resolvedYAML.contains("app_id: com.mydomain.myapp"))
    XCTAssertTrue(resolvedYAML.contains("virtual_clusters: [test]"))
  }

  func testReturnsNilWhenUnresolvedValueInTemplate() {
    let config = EnvoyConfiguration(statsDomain: "stats.foo.com",
                                    connectTimeoutSeconds: 200,
                                    dnsRefreshSeconds: 300,
                                    dnsFailureRefreshSecondsBase: 400,
                                    dnsFailureRefreshSecondsMax: 500,
                                    statsFlushSeconds: 600,
                                    appVersion: "v1.2.3",
                                    appId: "com.mydomain.myapp",
                                    virtualClusters: "[test]")
    XCTAssertNil(config.resolveTemplate("{{ missing }}"))
  }
}
