import Envoy
import XCTest

final class CounterTests: XCTestCase {
    func testConvenientMethodDelegatesToTheMainMethod() {
        class MockCounterImpl: Counter {
            var count: Int?
            func increment(count: Int) {
                self.count = count
            }
        }

        let counter = MockCounterImpl()
        counter.increment()
        XCTAssertEqual(1, counter.count)
    }
}
