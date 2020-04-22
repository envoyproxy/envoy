@testable import Envoy
import Foundation
import XCTest

final class EnvoyClientTests: XCTestCase {
  override func tearDown() {
    super.tearDown()
    MockEnvoyHTTPStream.reset()
  }

  func testNonStreamingExtensionSendsRequestDetailsThroughStream() throws {
    let requestExpectation = self.expectation(description: "Sends request")
    let dataExpectation = self.expectation(description: "Sends data")
    let closeExpectation = self.expectation(description: "Calls close")

    let expectedRequest = RequestBuilder(
      method: .get, scheme: "https", authority: "www.envoyproxy.io", path: "/docs")
      .build()
    let expectedData = Data([1, 2, 3])
    let expectedTrailers = ["foo": ["bar", "baz"]]

    MockEnvoyHTTPStream.onHeaders = { headers, closeStream in
      XCTAssertEqual(expectedRequest.outboundHeaders(), headers)
      XCTAssertFalse(closeStream)
      requestExpectation.fulfill()
    }

    MockEnvoyHTTPStream.onData = { data, closeStream in
      XCTAssertEqual(expectedData, data)
      XCTAssertFalse(closeStream)
      dataExpectation.fulfill()
    }

    MockEnvoyHTTPStream.onTrailers = { trailers in
      XCTAssertEqual(expectedTrailers, trailers)
      closeExpectation.fulfill()
    }

    let envoy = try EnvoyClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .build()
    envoy.send(expectedRequest, body: expectedData, trailers: expectedTrailers,
               handler: ResponseHandler())
    self.wait(for: [requestExpectation, dataExpectation, closeExpectation],
              timeout: 0.1, enforceOrder: true)
  }
}
