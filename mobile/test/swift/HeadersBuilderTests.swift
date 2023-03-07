@testable import Envoy
import XCTest

private let kRetryPolicy = RetryPolicy(maxRetryCount: 123,
                                       retryOn: [.connectFailure, .status5xx],
                                       perRetryTimeoutMS: 9000)

final class HeadersBuilderTests: XCTestCase {
  func testAddingNewHeaderAddsToListOfHeaderKeys() {
    let headers = HeadersBuilder(headers: [:])
      .add(name: "x-foo", value: "1")
      .add(name: "x-foo", value: "2")
      .caseSensitiveHeaders()
    XCTAssertEqual(["1", "2"], headers["x-foo"])
  }

  func testRemovingSpecificHeaderKeyRemovesAllOfItsValues() {
    let headers = HeadersBuilder(headers: [:])
      .add(name: "x-foo", value: "1")
      .add(name: "x-foo", value: "2")
      .remove(name: "x-foo")
      .caseSensitiveHeaders()
    XCTAssertNil(headers["x-foo"])
  }

  func testRemovingSpecificHeaderKeyDoesNotRemoveOtherKeys() {
    let headers = HeadersBuilder(headers: [:])
      .add(name: "x-foo", value: "123")
      .add(name: "x-bar", value: "abc")
      .remove(name: "x-foo")
      .caseSensitiveHeaders()
    XCTAssertEqual(["x-bar": ["abc"]], headers)
  }

  func testSettingHeaderReplacesExistingHeadersWithMatchingName() {
    let headers = HeadersBuilder(headers: [:])
      .add(name: "x-foo", value: "123")
      .set(name: "x-foo", value: ["abc"])
      .caseSensitiveHeaders()
    XCTAssertEqual(["x-foo": ["abc"]], headers)
  }

  func testInitializationIsCaseInsensitivePreservesCasingAndProcessesInAlphabeticalOrder() {
    let headers = HeadersBuilder(headers: ["a": ["456"], "A": ["123"]])
    XCTAssertEqual(["A": ["123", "456"]], headers.caseSensitiveHeaders())
  }

  func testAddingHeaderIsCaseInsensitiveAndHeaderCasingIsPreserved() {
    let headers = HeadersBuilder(headers: [:])
    headers.add(name: "fOo", value: "abc")
    headers.add(name: "foo", value: "123")
    XCTAssertEqual(["fOo": ["abc", "123"]], headers.caseSensitiveHeaders())
  }

  func testSettingHeaderIsCaseInsensitiveAndHeaderCasingIsPreserved() {
    let headers = HeadersBuilder(headers: [:])
    headers.set(name: "foo", value: ["123"])
    headers.set(name: "fOo", value: ["abc"])
    XCTAssertEqual(["fOo": ["abc"]], headers.caseSensitiveHeaders())
  }

  func testRemovingHeaderIsCaseInsensitive() {
    let headers = HeadersBuilder(headers: [:])
    headers.set(name: "foo", value: ["123"])
    headers.remove(name: "fOo")
    XCTAssertEqual([:], headers.caseSensitiveHeaders())
  }

  func testRestrictedHeadersAreNotSettable() {
    let headers = RequestHeadersBuilder(method: .get, authority: "example.com", path: "/")
      .add(name: "host", value: "example.com")
      .add(name: "hostWithSuffix", value: "foo.bar")
      .set(name: ":scheme", value: ["http"])
      .set(name: ":path", value: ["/nope"])
      .caseSensitiveHeaders()
    let expected = [
      ":authority": ["example.com"],
      ":path": ["/"],
      ":method": ["GET"],
      ":scheme": ["https"],
      "hostWithSuffix": ["foo.bar"],
    ]
    XCTAssertEqual(expected, headers)
  }

  func testBuildersAreEqualIfUnderlyingHeadersAreEqual() {
    let builder1 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]])
    let builder2 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]])
    XCTAssertEqual(builder1, builder2)
  }

  func testHeadersAreEqualIfUnderlyingHeadersAreEqual() {
    let headers1 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]]).build()
    let headers2 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]]).build()
    XCTAssertEqual(headers1, headers2)
  }

  func testBuilderPointersAreNotEqualWhenInstancesAreDifferent() {
    let builder1 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]])
    let builder2 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]])
    XCTAssert(builder1 !== builder2)
  }

  func testHeadersPointersAreNotEqualWhenInstancesAreDifferent() {
    let headers1 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]]).build()
    let headers2 = RequestHeadersBuilder(headers: ["x-foo": ["123"], "x-bar": ["abc"]]).build()
    XCTAssert(headers1 !== headers2)
  }
}
