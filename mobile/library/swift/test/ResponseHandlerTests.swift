@testable import Envoy
import Foundation
import XCTest

final class ResponseHandlerTests: XCTestCase {
  func testParsingStatusCodeFromHeadersReturnsFirstStatus() {
    let headers = [":status": ["204", "200"], "other": ["1"]]
    XCTAssertEqual(204, ResponseHandler.statusCode(fromHeaders: headers))
  }

  func testParsingInvalidStatusCodeReturnsZero() {
    let headers = [":status": ["invalid"], "other": ["1"]]
    XCTAssertEqual(0, ResponseHandler.statusCode(fromHeaders: headers))
  }

  func testParsingMissingStatusCodeReturnsZero() {
    XCTAssertEqual(0, ResponseHandler.statusCode(fromHeaders: [:]))
  }
}
