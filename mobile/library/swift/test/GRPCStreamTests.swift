@testable import Envoy
import EnvoyEngine
import Foundation
import XCTest

private let kMessage1 = Data([1, 2, 3, 4, 5])
private let kMessage2 = Data([6, 7, 8, 9, 0, 1])

final class GRPCStreamTests: XCTestCase {
  // MARK: - Request tests

  func testDataSizeIsFiveBytesGreaterThanMessageSize() {
    var sentData = Data()
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      let stream = MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
      stream.onData = { data, _ in sentData.append(data) }
      return stream
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .start()
      .sendMessage(kMessage1)

    XCTAssertEqual(5 + kMessage1.count, sentData.count)
  }

  func testPrefixesSentDataWithZeroCompressionFlag() {
    var sentData = Data()
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      let stream = MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
      stream.onData = { data, _ in sentData.append(data) }
      return stream
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .start()
      .sendMessage(kMessage1)

    XCTAssertEqual(UInt8(0), sentData.integer(atIndex: 0))
  }

  func testPrefixesSentDataWithBigEndianLengthOfMessage() {
    var sentData = Data()
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      let stream = MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
      stream.onData = { data, _ in sentData.append(data) }
      return stream
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .start()
      .sendMessage(kMessage1)

    let expectedMessageLength = UInt32(kMessage1.count).bigEndian
    let messageLength: UInt32? = sentData.integer(atIndex: 1)
    XCTAssertEqual(expectedMessageLength, messageLength)
  }

  func testAppendsMessageDataAtTheEndOfSentData() {
    var sentData = Data()
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      let stream = MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
      stream.onData = { data, _ in sentData.append(data) }
      return stream
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .start()
      .sendMessage(kMessage1)

    XCTAssertEqual(kMessage1, sentData.subdata(in: 5..<sentData.count))
  }

  func testCloseIsCalledWithEmptyDataFrame() {
    var closedData: Data?
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      let stream = MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
      stream.onData = { data, endStream in
        XCTAssertTrue(endStream)
        closedData = data
      }
      return stream
    }

    GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .start()
      .close()

    XCTAssertEqual(Data(), closedData)
  }

  // MARK: - Response tests

  func testHeadersCallbackPassesHeaders() {
    let expectation = self.expectation(description: "Closure is called")
    let expectedHeaders = ResponseHeaders(headers: ["grpc-status": ["1"], "other": ["foo", "bar"]])

    var streamCallbacks: EnvoyHTTPCallbacks!
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      streamCallbacks = callbacks
      return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .setOnResponseHeaders { headers, endStream in
        XCTAssertEqual(expectedHeaders, headers)
        XCTAssertTrue(endStream)
        expectation.fulfill()
      }
      .start()

    streamCallbacks.onHeaders(expectedHeaders.headers, true)
    self.waitForExpectations(timeout: 0.1)
  }

  func testTrailersCallbackPassesTrailers() {
    let expectation = self.expectation(description: "Closure is called")
    let expectedTrailers = ResponseTrailers(headers: ["foo": ["bar"], "baz": ["1", "2"]])

    var streamCallbacks: EnvoyHTTPCallbacks!
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      streamCallbacks = callbacks
      return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .setOnResponseTrailers { trailers in
        XCTAssertEqual(expectedTrailers, trailers)
        expectation.fulfill()
      }
      .start()

    streamCallbacks.onTrailers(expectedTrailers.headers)
    self.waitForExpectations(timeout: 0.1)
  }

  func testMessageCallbackBuffersDataSentInSingleChunk() {
    let expectation = self.expectation(description: "Closure is called")
    let firstMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x5, // Length bytes
    ] + kMessage1)

    var streamCallbacks: EnvoyHTTPCallbacks!
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      streamCallbacks = callbacks
      return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .setOnResponseMessage { message in
        XCTAssertEqual(kMessage1, message)
        expectation.fulfill()
      }
      .start()

    streamCallbacks.onData(firstMessage, false)
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
    var streamCallbacks: EnvoyHTTPCallbacks!
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      streamCallbacks = callbacks
      return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .setOnResponseMessage { message in
        XCTAssertEqual(expectedMessages.removeFirst(), message)
        expectation.fulfill()
      }
      .start()

    streamCallbacks.onData(firstMessage, false)
    streamCallbacks.onData(secondMessagePart1, false)
    streamCallbacks.onData(secondMessagePart2, false)
    streamCallbacks.onData(secondMessagePart3, false)
    self.waitForExpectations(timeout: 0.1)
    XCTAssertTrue(expectedMessages.isEmpty)
  }

  func testMessageCallbackCanBeCalledWithZeroLengthMessage() {
    let expectation = self.expectation(description: "Closure is called")
    let firstMessage = Data([
      0x0, // Compression flag
      0x0, 0x0, 0x0, 0x0, // Length bytes
    ])

    var streamCallbacks: EnvoyHTTPCallbacks!
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      streamCallbacks = callbacks
      return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .setOnResponseMessage { message in
        XCTAssertTrue(message.isEmpty)
        expectation.fulfill()
      }
      .start()

    streamCallbacks.onData(firstMessage, false)
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
    var streamCallbacks: EnvoyHTTPCallbacks!
    let engine = MockEnvoyEngine { callbacks -> EnvoyHTTPStream in
      streamCallbacks = callbacks
      return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks)
    }

    _ = GRPCStreamPrototype(underlyingStream: StreamPrototype(engine: engine))
      .setOnResponseMessage { message in
        XCTAssertEqual(expectedMessages.removeFirst(), message)
        expectation.fulfill()
      }
      .start()

    streamCallbacks.onData(firstMessage, false)
    streamCallbacks.onData(secondMessage, false)
    self.waitForExpectations(timeout: 0.1)
    XCTAssertTrue(expectedMessages.isEmpty)
  }
}
