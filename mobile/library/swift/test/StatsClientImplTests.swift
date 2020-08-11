@testable import Envoy
import EnvoyEngine
import Foundation
import XCTest

final class StatsClientImplTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRecordCounter = nil
  }

  func testCounterDelegatesToEngine() {
    var actualSeries: String?
    var actualCount: UInt?
    MockEnvoyEngine.onRecordCounter = { series, count in
      actualSeries = series
      actualCount = count
    }
    let mockEngine = MockEnvoyEngine()
    let statsClient = StatsClientImpl(engine: mockEngine)
    let counter = statsClient.counter(elements: ["test", "stat"])
    counter.increment()
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualCount, 1)
  }

  func testCounterDelegatesToEngineWithCount() {
    var actualSeries: String?
    var actualCount: UInt?
    MockEnvoyEngine.onRecordCounter = { series, count in
      actualSeries = series
      actualCount = count
    }
    let mockEngine = MockEnvoyEngine()
    let statsClient = StatsClientImpl(engine: mockEngine)
    let counter = statsClient.counter(elements: ["test", "stat"])
    counter.increment(count: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualCount, 5)
  }

  func testCounterWeaklyHoldsEngine() {
    let mockEngine = MockEnvoyEngine()
    let statsClient = StatsClientImpl(engine: mockEngine)
    let counter = statsClient.counter(elements: ["test", "stat"])
    weak var weakEngine = mockEngine

    addTeardownBlock { [counter, weak weakEngine] in
      XCTAssertNotNil(counter) // Counter is captured and still exists.
      XCTAssertNil(weakEngine) // weakEngine is nil (and so Counter didn't keep it alive).
    }
  }
}
