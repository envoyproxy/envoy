@testable import Envoy
import Foundation
import XCTest

private let kMessage1 = Data([1, 2, 3, 4, 5])
private let kMessage2 = Data([6, 7, 8, 9, 0, 1])

final class GRPCStreamTests: XCTestCase {
  // MARK: - Request tests

  func testDataSizeIsFiveBytesGreaterThanMessageSize() {
    var sentData = Data()
    let streamClient = MockStreamClient { stream in
      stream.onRequestData = { data, _ in sentData.append(data) }
    }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .start()
      .sendMessage(kMessage1)

    XCTAssertEqual(5 + kMessage1.count, sentData.count)
  }

  func testPrefixesSentDataWithZeroCompressionFlag() {
    var sentData = Data()
    let streamClient = MockStreamClient { stream in
      stream.onRequestData = { data, _ in sentData.append(data) }
    }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .start()
      .sendMessage(kMessage1)

    XCTAssertEqual(UInt8(0), sentData.integer(atIndex: 0))
  }

  func testPrefixesSentDataWithBigEndianLengthOfMessage() {
    var sentData = Data()
    let streamClient = MockStreamClient { stream in
      stream.onRequestData = { data, _ in sentData.append(data) }
    }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .start()
      .sendMessage(kMessage1)

    let expectedMessageLength = UInt32(kMessage1.count).bigEndian
    let messageLength: UInt32? = sentData.integer(atIndex: 1)
    XCTAssertEqual(expectedMessageLength, messageLength)
  }

  func testAppendsMessageDataAtTheEndOfSentData() {
    var sentData = Data()
    let streamClient = MockStreamClient { stream in
      stream.onRequestData = { data, _ in sentData.append(data) }
    }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .start()
      .sendMessage(kMessage1)

    XCTAssertEqual(kMessage1, sentData.subdata(in: 5..<sentData.count))
  }

  func testCloseIsCalledWithEmptyDataFrame() {
    var closedData: Data?
    let streamClient = MockStreamClient { stream in
      stream.onRequestData = { data, endStream in
        XCTAssertTrue(endStream)
        closedData = data
      }
    }

    GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .start()
      .close()

    XCTAssertEqual(Data(), closedData)
  }

  // MARK: - Response tests

  func testHeadersCallbackPassesHeaders() {
    let expectation = self.expectation(description: "Closure is called")
    let expectedHeaders = ResponseHeaders(
      headers: ["grpc-status": ["1"], "x-other": ["foo", "bar"]])

    var stream: MockStream!
    let streamClient = MockStreamClient { stream = $0 }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseHeaders { headers, endStream in
        XCTAssertEqual(expectedHeaders, headers)
        XCTAssertTrue(endStream)
        expectation.fulfill()
      }
      .start()

    stream.receiveHeaders(expectedHeaders, endStream: true)
    self.waitForExpectations(timeout: 0.1)
  }

  func testTrailersCallbackPassesTrailers() {
    let expectation = self.expectation(description: "Closure is called")
    let expectedTrailers = ResponseTrailers(headers: ["x-foo": ["bar"], "x-baz": ["1", "2"]])

    var stream: MockStream!
    let streamClient = MockStreamClient { stream = $0 }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseTrailers { trailers in
        XCTAssertEqual(expectedTrailers, trailers)
        expectation.fulfill()
      }
      .start()

    stream.receiveTrailers(expectedTrailers)
    self.waitForExpectations(timeout: 0.1)
  }

  func testMessageCallbackBuffersDataSentInSingleChunk() {
    let expectation = self.expectation(description: "Closure is called")
    let firstMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x5, // Length bytes
    ] + kMessage1)

    var stream: MockStream!
    let streamClient = MockStreamClient { stream = $0 }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message in
        XCTAssertEqual(kMessage1, message)
        expectation.fulfill()
      }
      .start()

    stream.receiveData(firstMessage, endStream: false)
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
    var stream: MockStream!
    let streamClient = MockStreamClient { stream = $0 }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message in
        XCTAssertEqual(expectedMessages.removeFirst(), message)
        expectation.fulfill()
      }
      .start()

    stream.receiveData(firstMessage, endStream: false)
    stream.receiveData(secondMessagePart1, endStream: false)
    stream.receiveData(secondMessagePart2, endStream: false)
    stream.receiveData(secondMessagePart3, endStream: false)
    self.waitForExpectations(timeout: 0.1)
    XCTAssertTrue(expectedMessages.isEmpty)
  }

  func testMessageCallbackCanBeCalledWithZeroLengthMessage() {
    let expectation = self.expectation(description: "Closure is called")
    let emptyMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x0, // Length bytes
    ])

    var stream: MockStream!
    let streamClient = MockStreamClient { stream = $0 }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message in
        XCTAssertTrue(message.isEmpty)
        expectation.fulfill()
      }
      .start()

    stream.receiveData(emptyMessage, endStream: false)
    self.waitForExpectations(timeout: 0.1)
  }

  func testMessageCallbackCanBeCalledWithMessageAfterZeroLengthMessage() {
    let expectation = self.expectation(description: "Closure is called")
    expectation.expectedFulfillmentCount = 2
    let emptyMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x0, // Length bytes
    ])

    let secondMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x6, // Length bytes
    ] + kMessage2)

    var expectedMessages = [Data(), kMessage2]
    var stream: MockStream!
    let streamClient = MockStreamClient { stream = $0 }

    _ = GRPCClient(streamClient: streamClient)
      .newGRPCStreamPrototype()
      .setOnResponseMessage { message in
        XCTAssertEqual(expectedMessages.removeFirst(), message)
        expectation.fulfill()
      }
      .start()

    stream.receiveData(emptyMessage, endStream: false)
    stream.receiveData(secondMessage, endStream: false)
    self.waitForExpectations(timeout: 0.1)
    XCTAssertTrue(expectedMessages.isEmpty)
  }
}
