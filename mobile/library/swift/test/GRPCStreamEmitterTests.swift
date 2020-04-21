@testable import Envoy
import Foundation
import XCTest

private let kMessageData = Data([1, 2, 3, 4])

private final class MockEmitter: StreamEmitter {
  private let onSendData: (_ data: Data) -> Void
  private let onCloseWithData: (_ data: Data) -> Void

  init(onSendData: @escaping (_ data: Data) -> Void,
       onCloseWithData: @escaping (_ data: Data) -> Void = { _ in })
  {
    self.onSendData = onSendData
    self.onCloseWithData = onCloseWithData
  }

  func sendData(_ data: Data) -> StreamEmitter {
    self.onSendData(data)
    return self
  }

  func close(trailers: [String: [String]]) {}

  func close(data: Data) {
    self.onCloseWithData(data)
  }

  func cancel() {}
}

final class GRPCStreamEmitterTests: XCTestCase {
  func testDataSizeIsFiveBytesGreaterThanMessageSize() {
    var sentData = Data()
    let mockEmitter = MockEmitter(onSendData: { sentData.append(contentsOf: $0) })
    GRPCStreamEmitter(emitter: mockEmitter)
      .sendMessage(kMessageData)
    XCTAssertEqual(5 + kMessageData.count, sentData.count)
  }

  func testPrefixesSentDataWithZeroCompressionFlag() {
    var sentData = Data()
    let mockEmitter = MockEmitter(onSendData: { sentData.append(contentsOf: $0) })
    GRPCStreamEmitter(emitter: mockEmitter)
      .sendMessage(kMessageData)
    XCTAssertEqual(UInt8(0), sentData.integer(atIndex: 0))
  }

  func testPrefixesSentDataWithBigEndianLengthOfMessage() {
    var sentData = Data()
    let mockEmitter = MockEmitter(onSendData: { sentData.append(contentsOf: $0) })
    GRPCStreamEmitter(emitter: mockEmitter)
      .sendMessage(kMessageData)

    let expectedMessageLength = UInt32(kMessageData.count).bigEndian
    let messageLength: UInt32? = sentData.integer(atIndex: 1)
    XCTAssertEqual(expectedMessageLength, messageLength)
  }

  func testAppendsMessageDataAtTheEndOfSentData() {
    var sentData = Data()
    let mockEmitter = MockEmitter(onSendData: { sentData.append(contentsOf: $0) })
    GRPCStreamEmitter(emitter: mockEmitter)
      .sendMessage(kMessageData)
    XCTAssertEqual(kMessageData, sentData.subdata(in: 5..<sentData.count))
  }

  func testCloseIsCalledWithEmptyDataFrame() {
    var closedData: Data?
    let mockEmitter = MockEmitter(onSendData: { _ in },
                                  onCloseWithData: { closedData = $0 })
    GRPCStreamEmitter(emitter: mockEmitter)
      .close()
    XCTAssertEqual(Data(), closedData)
  }
}
