@testable import Envoy
import Foundation
import XCTest

private let kMessage1 = Data([1, 2, 3, 4, 5])
private let kMessage2 = Data([6, 7, 8, 9, 0, 1])

final class GRPCResponseHandlerTests: XCTestCase {
  func testHeadersCallbackPassesHeadersAndGRPCStatus() {
    let expectation = self.expectation(description: "Closure is called")
    let expectedHeaders: [String: [String]] = ["grpc-status": ["1"], "other": ["foo", "bar"]]
    let handler = GRPCResponseHandler()
      .onHeaders { headers, grpcStatus, endStream in
        XCTAssertEqual(expectedHeaders, headers)
        XCTAssertEqual(1, grpcStatus)
        XCTAssertTrue(endStream)
        expectation.fulfill()
      }

    handler.underlyingHandler.underlyingCallbacks.onHeaders(expectedHeaders, true)
    self.waitForExpectations(timeout: 0.1)
  }

  func testTrailersCallbackPassesTrailers() {
    let expectation = self.expectation(description: "Closure is called")
    let expectedTrailers: [String: [String]] = ["foo": ["bar"], "baz": ["1", "2"]]
    let handler = GRPCResponseHandler()
      .onTrailers { trailers in
        XCTAssertEqual(expectedTrailers, trailers)
        expectation.fulfill()
      }

    handler.underlyingHandler.underlyingCallbacks.onTrailers(expectedTrailers)
    self.waitForExpectations(timeout: 0.1)
  }

  func testMessageCallbackBuffersDataSentInSingleChunk() {
    let expectation = self.expectation(description: "Closure is called")
    let firstMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x5, // Length bytes
    ] + kMessage1)

    let handler = GRPCResponseHandler()
      .onMessage { message in
        XCTAssertEqual(kMessage1, message)
        expectation.fulfill()
      }

    handler.underlyingHandler.underlyingCallbacks.onData(firstMessage, false)
    self.waitForExpectations(timeout: 0.1)
  }

  func testMessageCallbackBuffersDataSentInMultipleChunks() {
    let expectation = self.expectation(description: "Closure is called")
    expectation.expectedFulfillmentCount = 2

    let firstMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x5, // Length bytes
    ] + kMessage1)

    let secondMessagePart1 = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, // 3/4 length bytes
    ])

    let secondMessagePart2 = Data([
      0x6, // Last length byte
    ] + kMessage2[0..<2])

    let secondMessagePart3 = Data(kMessage2[2..<6])

    var expectedMessages = [kMessage1, kMessage2]
    let handler = GRPCResponseHandler()
      .onMessage { message in
        XCTAssertEqual(expectedMessages.removeFirst(), message)
        expectation.fulfill()
      }

    handler.underlyingHandler.underlyingCallbacks.onData(firstMessage, false)
    handler.underlyingHandler.underlyingCallbacks.onData(secondMessagePart1, false)
    handler.underlyingHandler.underlyingCallbacks.onData(secondMessagePart2, false)
    handler.underlyingHandler.underlyingCallbacks.onData(secondMessagePart3, false)
    self.waitForExpectations(timeout: 0.1)
    XCTAssertTrue(expectedMessages.isEmpty)
  }

  func testMessageCallbackCanBeCalledWithZeroLengthMessage() {
    let expectation = self.expectation(description: "Closure is called")
    let firstMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x0, // Length bytes
    ])

    let handler = GRPCResponseHandler()
      .onMessage { message in
        XCTAssertTrue(message.isEmpty)
        expectation.fulfill()
      }

    handler.underlyingHandler.underlyingCallbacks.onData(firstMessage, false)
    self.waitForExpectations(timeout: 0.1)
  }

  func testMessageCallbackCanBeCalledWithMessageAfterZeroLengthMessage() {
    let expectation = self.expectation(description: "Closure is called")
    expectation.expectedFulfillmentCount = 2
    let firstMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x0, // Length bytes
    ])

    let secondMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x6, // Length bytes
    ] + kMessage2)

    var expectedMessages = [Data(), kMessage2]
    let handler = GRPCResponseHandler()
      .onMessage { message in
        XCTAssertEqual(expectedMessages.removeFirst(), message)
        expectation.fulfill()
      }

    handler.underlyingHandler.underlyingCallbacks.onData(firstMessage, false)
    handler.underlyingHandler.underlyingCallbacks.onData(secondMessage, false)
    self.waitForExpectations(timeout: 0.1)
    XCTAssertTrue(expectedMessages.isEmpty)
  }

  // MARK: - gRPC status parsing

  func testParsingGRPCStatusFromHeadersReturnsFirstStatus() {
    let headers = [":status": ["200"], "grpc-status": ["1", "2"]]
    XCTAssertEqual(1, GRPCResponseHandler.grpcStatus(fromHeaders: headers))
  }

  func testParsingInvalidGRPCStatusReturnsZero() {
    let headers = ["grpc-status": ["invalid"]]
    XCTAssertEqual(0, GRPCResponseHandler.grpcStatus(fromHeaders: headers))
  }

  func testParsingMissingGRPCStatusReturnsZero() {
    XCTAssertEqual(0, GRPCResponseHandler.grpcStatus(fromHeaders: [:]))
  }
}
