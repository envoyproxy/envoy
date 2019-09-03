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
  static var onRun: ((_ config: String, _ logLevel: String?) -> Void)?

  func run(withConfig config: String) -> Int32 {
    MockEnvoyEngine.onRun?(config, nil)
    return 0
  }

  func run(withConfig config: String, logLevel: String) -> Int32 {
    MockEnvoyEngine.onRun?(config, logLevel)
    return 0
  }

  func setup() {}

  func startStream(with observer: EnvoyObserver) -> EnvoyHTTPStream {
    return MockEnvoyHTTPStream(handle: 0, observer: observer)
  }
}

final class EnvoyBuilderTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRun = nil
  }

  func testAddingCustomConfigYAMLUsesSpecifiedYAMLWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRun = { yaml, _ in
      XCTAssertEqual("foobar", yaml)
      expectation.fulfill()
    }

    _ = try EnvoyBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addConfigYAML("foobar")
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testAddingLogLevelAddsLogLevelWhenRunningEnvoy() throws {
    let expectation = self.expectation(description: "Run called with expected data")
    MockEnvoyEngine.onRun = { _, logLevel in
      XCTAssertEqual("trace", logLevel)
      expectation.fulfill()
    }

    _ = try EnvoyBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addLogLevel(.trace)
      .build()
    self.waitForExpectations(timeout: 0.01)
  }

  func testResolvesYAMLWithConnectTimeout() throws {
    let resolvedYAML = try EnvoyBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addConnectTimeoutSeconds(200)
      .resolvedYAML(kMockTemplate)

    XCTAssertTrue(resolvedYAML.contains("connect_timeout: 200s"))
  }

  func testResolvesYAMLWithDNSRefreshSeconds() throws {
    let resolvedYAML = try EnvoyBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addDNSRefreshSeconds(200)
      .resolvedYAML(kMockTemplate)

    XCTAssertTrue(resolvedYAML.contains("dns_refresh_rate: 200s"))
  }

  func testResolvesYAMLWithStatsFlushSeconds() throws {
    let resolvedYAML = try EnvoyBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .addStatsFlushSeconds(200)
      .resolvedYAML(kMockTemplate)

    XCTAssertTrue(resolvedYAML.contains("stats_flush_interval: 200s"))
  }

  func testThrowsWhenUnresolvedValueInTemplate() {
    let builder = EnvoyBuilder().addEngineType(MockEnvoyEngine.self)
    XCTAssertThrowsError(try builder.resolvedYAML("{{ missing }}")) { error in
      XCTAssertEqual(.unresolvedTemplateKey, error as? EnvoyBuilderError)
    }
  }
}
