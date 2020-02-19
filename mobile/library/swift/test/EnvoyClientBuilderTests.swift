@testable import Envoy
import Foundation
import XCTest

private let kMockTemplate = """
mock_template:
- name: mock
  stats_domain: {{ stats_domain }}
  device_os: {{ device_os }}
  connect_timeout: {{ connect_timeout_seconds }}s
  dns_refresh_rate: {{ dns_refresh_rate_seconds }}s
  stats_flush_interval: {{ stats_flush_interval_seconds }}s
"""

private final class MockEnvoyEngine: NSObject, EnvoyEngine {
  static var onRunWithConfig: ((_ config: EnvoyConfiguration, _ logLevel: String?) -> Void)?
  static var onRunWithYAML: ((_ configYAML: String, _ logLevel: String?) -> Void)?

  func run(withConfig config: EnvoyConfiguration, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRunWithConfig?(config, logLevel)
    return 0
  }

  func run(withConfigYAML configYAML: String, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRunWithYAML?(configYAML, logLevel)
    return 0
  }

  func startStream(with callbacks: EnvoyHTTPCallbacks) -> EnvoyHTTPStream {
    return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
  }
}

final class EnvoyClientBuilderTests: XCTestCase {
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

    _ = try EnvoyClientBuilder(yaml: "foobar")
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

    _ = try EnvoyClientBuilder()
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

    _ = try EnvoyClientBuilder()
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

    _ = try EnvoyClientBuilder()
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

    _ = try EnvoyClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSRefreshSeconds(23)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingStatsFlushSecondsAddsToConfigurationWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithConfig = { config, _ in
      XCTAssertEqual(42, config.statsFlushSeconds)
      expectation.fulfill()
    }

    _ = try EnvoyClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addStatsFlushSeconds(42)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testResolvesYAMLWithIndividuallySetValues() throws {
    let config = EnvoyConfiguration(statsDomain: "stats.foo.com",
                                    connectTimeoutSeconds: 200,
                                    dnsRefreshSeconds: 300,
                                    statsFlushSeconds: 400)
    guard let resolvedYAML = config.resolveTemplate(kMockTemplate) else {
      XCTFail("Resolved template YAML is nil")
      return
    }

    XCTAssertTrue(resolvedYAML.contains("stats_domain: stats.foo.com"))
    XCTAssertTrue(resolvedYAML.contains("connect_timeout: 200s"))
    XCTAssertTrue(resolvedYAML.contains("dns_refresh_rate: 300s"))
    XCTAssertTrue(resolvedYAML.contains("stats_flush_interval: 400s"))
    XCTAssertTrue(resolvedYAML.contains("device_os: iOS"))
  }

  func testReturnsNilWhenUnresolvedValueInTemplate() {
    let config = EnvoyConfiguration(statsDomain: "stats.foo.com",
                                    connectTimeoutSeconds: 200,
                                    dnsRefreshSeconds: 300,
                                    statsFlushSeconds: 400)
    XCTAssertNil(config.resolveTemplate("{{ missing }}"))
  }
}
