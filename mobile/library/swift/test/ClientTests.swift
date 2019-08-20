import Envoy
import Foundation
import XCTest

private final class MockStreamEmitter: StreamEmitter {
  var onData: ((Data?) -> Void)?
  var onTrailers: (([String: [String]]) -> Void)?

  func sendData(_ data: Data) -> StreamEmitter {
    self.onData?(data)
    return self
  }

  func sendMetadata(_ metadata: [String: [String]]) -> StreamEmitter {
    return self
  }

  func close(trailers: [String: [String]]) {
    self.onTrailers?(trailers)
  }

  func cancel() {}
}

private final class MockClient: Client {
  var onRequest: ((Request) -> Void)?
  var onData: ((Data?) -> Void)?
  var onTrailers: (([String: [String]]) -> Void)?

  func send(_ request: Request, handler: ResponseHandler) -> StreamEmitter {
    self.onRequest?(request)
    let emitter = MockStreamEmitter()
    emitter.onData = self.onData
    emitter.onTrailers = self.onTrailers
    return emitter
  }
}

final class ClientTests: XCTestCase {
  func testNonStreamingExtensionSendsRequestDetailsThroughStream() {
    let requestExpectation = self.expectation(description: "Sends request")
    let dataExpectation = self.expectation(description: "Sends data")
    let closeExpectation = self.expectation(description: "Calls close")

    let expectedRequest = RequestBuilder(
      method: .get, scheme: "https", authority: "www.envoyproxy.io", path: "/docs")
      .build()
    let expectedData = Data([1, 2, 3])
    let expectedTrailers = ["foo": ["bar", "baz"]]

    let mockClient = MockClient()
    mockClient.onRequest = { request in
      XCTAssertEqual(expectedRequest, request)
      requestExpectation.fulfill()
    }

    mockClient.onData = { data in
      XCTAssertEqual(expectedData, data)
      dataExpectation.fulfill()
    }

    mockClient.onTrailers = { trailers in
      XCTAssertEqual(expectedTrailers, trailers)
      closeExpectation.fulfill()
    }

    mockClient.send(expectedRequest, data: expectedData, trailers: expectedTrailers,
                    handler: ResponseHandler())
    self.wait(for: [requestExpectation, dataExpectation, closeExpectation],
              timeout: 0.1, enforceOrder: true)
  }
}
