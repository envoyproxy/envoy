@testable import Envoy
import XCTest

final class ResponseHeadersTests: XCTestCase {
  func testParsingStatusCodeFromHeadersReturnsFirstStatus() {
    let headers = [":status": ["204", "200"], "other": ["1"]]
    XCTAssertEqual(204, ResponseHeaders(headers: headers).httpStatus)
  }

  func testParsingInvalidStatusCodeReturnsNil() {
    let headers = [":status": ["invalid"], "other": ["1"]]
    XCTAssertNil(ResponseHeaders(headers: headers).httpStatus)
  }

  func testParsingMissingStatusCodeReturnsNil() {
    XCTAssertNil(ResponseHeaders(headers: [:]).httpStatus)
  }

  func testAddingHttpStatusCodeSetsTheAppropriateHeader() {
    let headers = ResponseHeadersBuilder()
      .addHttpStatus(200)
      .build()
    XCTAssertEqual(200, headers.httpStatus)
  }
}
