import Envoy
import Foundation
import XCTest

final class StatFlushIntegrationTest: XCTestCase {
  func testLotsOfFlushesWithHistograms() throws {
    let engineExpectation = self.expectation(description: "Engine Running")

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .addStatsFlushSeconds(1)
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 10), .completed)

    let pulseClient = engine.pulseClient()
    let distribution = pulseClient.distribution(elements: ["foo", "bar", "distribution"])

    distribution.recordValue(value: 100)

    // Hit flushStats() many times in a row to make sure that there are no issues with
    // concurrent flushing.
    for _ in 0...100 {
        engine.flushStats()
    }

    engine.terminate()
  }

  func testMultipleStatSinks() throws {
    let engineExpectation = self.expectation(description: "Engine Running")

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .addStatsFlushSeconds(1)
      .addGrpcStatsDomain("example.com")
      .addStatsSinks(
        [statsdSinkConfig(port: 1234), statsdSinkConfig(port: 5555)]
      )
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 10), .completed)

    engine.terminate()
  }

  func statsdSinkConfig(port: Int) -> String {
  return """
    { name: envoy.stat_sinks.statsd,
      typed_config: {
        "@type": type.googleapis.com/envoy.config.metrics.v3.StatsdSink,
        address: { socket_address: { address: 127.0.0.1, port_value: \(port) } }
      }
    }
"""
  }
}
