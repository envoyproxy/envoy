@testable import Envoy
import Foundation
import XCTest

private let kMockTemplate = """
mock_template:
- name: mock
  connect_timeout: {{ connect_timeout }}
  dns_refresh_rate: {{ dns_refresh_rate }}
  stats_flush_interval: {{ stats_flush_interval }}
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

  func testAddingCustomConfigYAMLUsesSpecifiedYAMLWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRunWithYAML = { yaml, _ in
      XCTAssertEqual("foobar", yaml)
      expectation.fulfill()
    }

    _ = try EnvoyClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addConfigYAML("foobar")
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

  func testResolvesYAMLWithIndividuallySetValues() throws {
    let config = EnvoyConfiguration(connectTimeoutSeconds: 200,
                                    dnsRefreshSeconds: 300,
                                    statsFlushSeconds: 400)
    guard let resolvedYAML = config.resolveTemplate(kMockTemplate) else {
      XCTFail("Resolved template YAML is nil")
      return
    }

    XCTAssertTrue(resolvedYAML.contains("connect_timeout: 200s"))
    XCTAssertTrue(resolvedYAML.contains("dns_refresh_rate: 300s"))
    XCTAssertTrue(resolvedYAML.contains("stats_flush_interval: 400s"))
  }

  func testReturnsNilWhenUnresolvedValueInTemplate() {
    let config = EnvoyConfiguration(connectTimeoutSeconds: 200,
                                    dnsRefreshSeconds: 300,
                                    statsFlushSeconds: 400)
    XCTAssertNil(config.resolveTemplate("{{ missing }}"))
  }
}
