import Envoy
import Foundation
import XCTest

final class StatFlushIntegrationTest: XCTestCase {
  func testLotsOfFlushesWithHistograms() throws {
    let loggingExpectation = self.expectation(description: "Run used platform logger")

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .addStatsFlushSeconds(1)
      .setLogger { msg in
        if msg.contains("starting main dispatch loop") {
          loggingExpectation.fulfill()
        }
      }
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [loggingExpectation], timeout: 3), .completed)

    let pulseClient = engine.pulseClient()
    let distribution = pulseClient.distribution(elements: ["foo", "bar", "distribution"])

    distribution.recordValue(value: 100)

    // Hit flushStats() many times in a row to make sure that there are no issues with
    // concurrent flushing.
    for _ in 0...100 {
        engine.flushStats()
    }
  }
}
