@testable import Envoy
import Foundation
import XCTest

private final class MockEnvoyEngine: EnvoyEngine {
  func run(withConfig config: EnvoyConfiguration, logLevel: String) -> Int32 {
    return 0
  }

  func run(withConfigYAML configYAML: String, logLevel: String) -> Int32 {
    return 0
  }

  func startStream(with callbacks: EnvoyHTTPCallbacks, bufferForRetry: Bool)
    -> EnvoyHTTPStream
  {
    return MockEnvoyHTTPStream(handle: 0, callbacks: callbacks,
                               bufferForRetry: bufferForRetry)
  }
}

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

  func testBuffersForRetriesWhenRetryPolicyIsSet() throws {
    let request = RequestBuilder(
      method: .get, scheme: "https", authority: "www.envoyproxy.io", path: "/docs")
      .addRetryPolicy(RetryPolicy(maxRetryCount: 3, retryOn: RetryRule.allCases))
      .build()
    let envoy = try EnvoyClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .build()
    envoy.send(request, body: Data(), trailers: [:], handler: ResponseHandler())

    XCTAssertEqual(true, MockEnvoyHTTPStream.bufferForRetry)
  }

  func testDoesNotBufferForRetriesWhenRetryPolicyIsNil() throws {
    let request = RequestBuilder(
      method: .get, scheme: "https", authority: "www.envoyproxy.io", path: "/docs")
      .build()
    let envoy = try EnvoyClientBuilder()
      .addEngineType(MockEnvoyEngine.self)
      .build()
    envoy.send(request, body: Data(), trailers: [:], handler: ResponseHandler())

    XCTAssertEqual(false, MockEnvoyHTTPStream.bufferForRetry)
  }
}
