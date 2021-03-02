@testable import Envoy
@testable import EnvoyEngine
import Foundation
import XCTest

final class PulseClientImplTests: XCTestCase {
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
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let counter = pulseClient.counter(elements: ["test", "stat"])
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
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let counter = pulseClient.counter(elements: ["test", "stat"])
    counter.increment(count: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualCount, 5)
  }

  func testCounterWeaklyHoldsEngine() {
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let counter = pulseClient.counter(elements: ["test", "stat"])
    weak var weakEngine = mockEngine

    addTeardownBlock { [counter, weak weakEngine] in
      XCTAssertNotNil(counter) // Counter is captured and still exists.
      XCTAssertNil(weakEngine) // weakEngine is nil (and so Counter didn't keep it alive).
    }
  }

  func testGaugeSetDelegatesToEngineWithValue() {
    var actualSeries: String?
    var actualValue: UInt?
    MockEnvoyEngine.onRecordGaugeSet = { series, value in
      actualSeries = series
      actualValue = value
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let gauge = pulseClient.gauge(elements: ["test", "stat"])
    gauge.set(value: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualValue, 5)
  }

  func testGaugeAddDelegatesToEngineWithAmount() {
    var actualSeries: String?
    var actualAmount: UInt?
    MockEnvoyEngine.onRecordGaugeAdd = { series, amount in
      actualSeries = series
      actualAmount = amount
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let gauge = pulseClient.gauge(elements: ["test", "stat"])
    gauge.add(amount: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualAmount, 5)
  }

  func testGaugeSubDelegatesToEngineWithAmount() {
    var actualSeries: String?
    var actualAmount: UInt?
    MockEnvoyEngine.onRecordGaugeSub = { series, amount in
      actualSeries = series
      actualAmount = amount
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let gauge = pulseClient.gauge(elements: ["test", "stat"])
    gauge.sub(amount: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualAmount, 5)
  }

  func testTimerCompleteWithDurationDelegatesToEngineWithValue() {
    var actualSeries: String?
    var actualDuration: UInt?
    MockEnvoyEngine.onRecordHistogramDuration = { series, duration in
      actualSeries = series
      actualDuration = duration
    }
    let mockEngine = MockEnvoyEngine()

    let pulseClient = PulseClientImpl(engine: mockEngine)
    let timer = pulseClient.timer(elements: ["test", "stat"])
    timer.completeWithDuration(durationMs: 5)

    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualDuration, 5)
  }

  func testDistributionRecordValueDelegatesToEngineWithValue() {
    var actualSeries: String?
    var actualValue: UInt?

    MockEnvoyEngine.onRecordHistogramValue = { series, value in
      actualSeries = series
      actualValue = value
    }
    let mockEngine = MockEnvoyEngine()

    let pulseClient = PulseClientImpl(engine: mockEngine)
    let distribution = pulseClient.distribution(elements: ["test", "stat"])
    distribution.recordValue(value: 5)

    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualValue, 5)
  }
}
