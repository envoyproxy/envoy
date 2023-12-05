import Envoy
import Foundation
import TestExtensions
import XCTest

final class EngineApiTest: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testEngineApis() throws {
    let engineExpectation = self.expectation(description: "Engine Running")

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 10), .completed)

    let pulseClient = engine.pulseClient()
    pulseClient.counter(elements: ["foo", "bar"]).increment(count: 1)

    XCTAssertTrue(engine.dumpStats().contains("foo.bar: 1"))

    engine.terminate()
  }
}
