import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class HTTPRequestUsingProxyTest: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testHTTRequestUsingProxy() throws {
    EnvoyTestServer.startHttpProxyServer()
    // let port = EnvoyTestServer.getEnvoyPort()

    let engineExpectation = self.expectation(description: "Run started engine")
    let responseHeadersExpectation = self.expectation(description: "Successful response headers received")
    // let responseDataExpectation = self.expectation(description: "Successful response data received")
    let responseTrailersExpectation = self.expectation(description: "Successful response trailers received")

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .enableProxying(true)
      .build()

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    // Send a request to trigger the test filter which should log an event.
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "neverssl.com", path: "/")
      .build()

    var responseBuffer = Data()
    engine.streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         print("==> AAB REQUEST HEADERS, endStream? ")
         print(endStream)
         XCTAssertEqual(200, responseHeaders.httpStatus)
         responseHeadersExpectation.fulfill()
      }
      .setOnResponseData { data, endStream, _ in
        print("==> AAB RESPONSE DATA, endStream? ")
        print(data.debugDescription)
        print(endStream)
        responseBuffer.append(contentsOf: data)
        // responseDataExpectation.fulfill()
      }
      .setOnResponseTrailers { trailers, _ in
        print("==> AAB RESPONSE TRAILERS")
        responseTrailersExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [responseHeadersExpectation], timeout: 10), .completed)
    // XCTAssertEqual(XCTWaiter.wait(for: [responseDataExpectation], timeout: 10), .completed)
    XCTAssertEqual(XCTWaiter.wait(for: [responseTrailersExpectation], timeout: 10), .completed)

    if let responseBody = String(data: responseBuffer, encoding: .utf8) {
      XCTAssertGreaterThanOrEqual(responseBody.utf8.count, 50)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }
}
