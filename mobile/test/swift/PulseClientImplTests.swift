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
}
