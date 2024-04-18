@testable import Envoy
@testable import EnvoyEngine
import Foundation
import XCTest

final class CounterTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyEngine.onRecordCounter = nil
  }

  func testConvenientMethodDelegatesToTheMainMethod() {
    class MockCounterImpl: Counter {
      var count: Int?
      var tags: Tags
      func increment(count: Int) {
        self.count = count
      }
      func increment(tags: Tags, count: Int) {
        self.tags = tags
        self.count = count
      }
      init(){
        self.count = -1
        self.tags = TagsBuilder().build()
      }
    }

    let counter = MockCounterImpl()
    counter.increment()
    XCTAssertEqual(1, counter.count)

    let tags = TagsBuilder().add(name: "testKey", value: "testValue").build()
    counter.increment(tags: tags, count: 1)
    XCTAssertEqual(1, counter.count)
    XCTAssertEqual(tags, counter.tags)
  }
}
