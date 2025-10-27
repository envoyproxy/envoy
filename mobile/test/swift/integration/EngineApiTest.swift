import Envoy
import Foundation
import TestExtensions
import XCTest

final class EngineApiTest: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  override static func tearDown() {
    super.tearDown()
    // Flush the stdout and stderror to show the print output.
    fflush(stdout)
    fflush(stderr)
  }

  func testEngineApis() throws {
    let engineExpectation = self.expectation(description: "Engine Running")

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
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
