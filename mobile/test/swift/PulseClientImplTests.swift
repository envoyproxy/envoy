@testable import Envoy
@testable import EnvoyEngine
import Foundation
import XCTest

final class PulseClientImplTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRecordCounter = nil
  }

  func testCounterDelegatesToEngineWithTagsAndCount() {
    var actualSeries: String?
    var actualTags = [String: String]()
    var actualCount: UInt?
    MockEnvoyEngine.onRecordCounter = { series, tags, count in
      actualSeries = series
      actualTags = tags
      actualCount = count
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let counter = pulseClient.counter(
      elements: ["test", "stat"],
      tags: TagsBuilder().add(name: "testKey", value: "testValue").build()
    )
    counter.increment()
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualTags, ["testKey": "testValue"])
    XCTAssertEqual(actualCount, 1)
  }

  func testCounterDelegatesToEngineWithCount() {
    var actualSeries: String?
    var actualTags = [String: String]()
    var actualCount: UInt?
    MockEnvoyEngine.onRecordCounter = { series, tags, count in
      actualSeries = series
      actualTags = tags
      actualCount = count
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)

    // Also verifies that counter can be created without tags
    let counter = pulseClient.counter(elements: ["test", "stat"])
    counter.increment(count: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualTags, [:])
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

  func testGaugeSetDelegatesToEngineWithTagsAndValue() {
    var actualSeries: String?
    var actualTags = [String: String]()
    var actualValue: UInt?
    MockEnvoyEngine.onRecordGaugeSet = { series, tags, value in
      actualSeries = series
      actualTags = tags
      actualValue = value
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let gauge = pulseClient.gauge(
      elements: ["test", "stat"],
      tags: TagsBuilder().add(name: "testKey", value: "testValue").build()
    )
    gauge.set(value: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualTags, ["testKey": "testValue"])
    XCTAssertEqual(actualValue, 5)
  }

  func testGaugeAddDelegatesToEngineWithAmount() {
    var actualSeries: String?
    var actualTags = [String: String]()
    var actualAmount: UInt?
    MockEnvoyEngine.onRecordGaugeAdd = { series, tags, amount in
      actualSeries = series
      actualTags = tags
      actualAmount = amount
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    // Also verifies that counter can be created without tags
    let gauge = pulseClient.gauge(elements: ["test", "stat"])
    gauge.add(amount: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualTags, [:])
    XCTAssertEqual(actualAmount, 5)
  }

  func testGaugeSubDelegatesToEngineWithAmount() {
    var actualSeries: String?
    var actualTags = [String: String]()
    var actualAmount: UInt?
    MockEnvoyEngine.onRecordGaugeSub = { series, tags, amount in
      actualSeries = series
      actualTags = tags
      actualAmount = amount
    }
    let mockEngine = MockEnvoyEngine()
    let pulseClient = PulseClientImpl(engine: mockEngine)
    let gauge = pulseClient.gauge(elements: ["test", "stat"])
    gauge.sub(amount: 5)
    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualTags, [:])
    XCTAssertEqual(actualAmount, 5)
  }

  func testTimerCompleteWithDurationDelegatesToEngineWithTagsAndValue() {
    var actualSeries: String?
    var actualTags = [String: String]()
    var actualDuration: UInt?
    MockEnvoyEngine.onRecordHistogramDuration = { series, tags, duration in
      actualSeries = series
      actualTags = tags
      actualDuration = duration
    }
    let mockEngine = MockEnvoyEngine()

    let pulseClient = PulseClientImpl(engine: mockEngine)
    let timer = pulseClient.timer(
      elements: ["test", "stat"],
      tags: TagsBuilder().add(name: "testKey", value: "testValue").build()
    )
    timer.completeWithDuration(durationMs: 5)

    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualTags, ["testKey": "testValue"])
    XCTAssertEqual(actualDuration, 5)
  }

  func testDistributionRecordValueDelegatesToEngineWithTagsAndValue() {
    var actualSeries: String?
    var actualTags = [String: String]()
    var actualValue: UInt?

    MockEnvoyEngine.onRecordHistogramValue = { series, tags, value in
      actualSeries = series
      actualTags = tags
      actualValue = value
    }
    let mockEngine = MockEnvoyEngine()

    let pulseClient = PulseClientImpl(engine: mockEngine)
    let distribution = pulseClient.distribution(
      elements: ["test", "stat"],
      tags: TagsBuilder().add(name: "testKey", value: "testValue").build()
    )
    distribution.recordValue(value: 5)

    XCTAssertEqual(actualSeries, "test.stat")
    XCTAssertEqual(actualTags, ["testKey": "testValue"])
    XCTAssertEqual(actualValue, 5)
  }
}
