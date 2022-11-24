@testable import Envoy
import XCTest

final class HeadersContainerTests: XCTestCase {
  func testInitializationPreservesAllHeadersFromInputHeadersMap() {
    let container = HeadersContainer(headers: ["a": ["456"], "b": ["123"]])
    XCTAssertEqual(["a": ["456"], "b": ["123"]], container.caseSensitiveHeaders())
  }

  func testInitializationIsCaseInsensitivePreservesCasingAndProcessesInAlphabeticalOrder() {
    let container = HeadersContainer(headers: ["a": ["456"], "A": ["123"]])
    XCTAssertEqual(["A": ["123", "456"]], container.caseSensitiveHeaders())
  }

  func testAddingHeaderAddsToListOfHeaders() {
    var container = HeadersContainer()
    container.add(name: "x-foo", value: "1")
    container.add(name: "x-foo", value: "2")

    XCTAssertEqual(["1", "2"], container.value(forName: "x-foo"))
  }

  func testAddingHeaderIsCaseInsensitiveAndPreservesHeaderNameCasing() {
    var container = HeadersContainer()
    container.add(name: "x-FOO", value: "1")
    container.add(name: "x-foo", value: "2")

    XCTAssertEqual(["1", "2"], container.value(forName: "x-foo"))
    XCTAssertEqual(["x-FOO": ["1", "2"]], container.caseSensitiveHeaders())
  }

  func testSettingHeaderAddsToListOfHeaders() {
    var container = HeadersContainer()
    container.set(name: "x-foo", value: ["abc"])

    XCTAssertEqual(["abc"], container.value(forName: "x-foo"))
  }

  func testSettingHeaderOverridesPreviousHeaderValues() {
    var container = HeadersContainer()
    container.add(name: "x-FOO", value: "1")
    container.add(name: "x-foo", value: "2")
    container.set(name: "x-foo", value: ["3"])

    XCTAssertEqual(["3"], container.value(forName: "x-foo"))
  }

  func testRemovingHeaderRemovesAllOfItsValues() {
    var container = HeadersContainer()
    container.add(name: "x-foo", value: "1")
    container.add(name: "x-foo", value: "2")
    container.remove(name: "x-foo")

    XCTAssertNil(container.value(forName: "x-foo"))
  }

  func testRemovingHeaderPerformsCaseInsensitiveHeaderNameLookup() {
    var container = HeadersContainer()
    container.add(name: "x-FOO", value: "1")
    container.add(name: "x-foo", value: "2")
    container.remove(name: "x-fOo")

    XCTAssertNil(container.value(forName: "x-foo"))
  }

  func testLookupIsCaseInsensitive() {
    var container = HeadersContainer()
    container.add(name: "x-FOO", value: "1")

    XCTAssertEqual(["1"], container.value(forName: "x-foo"))
    XCTAssertEqual(["1"], container.value(forName: "x-fOo"))
    XCTAssertEqual(["1"], container.value(forName: "x-FOO"))
  }
}
