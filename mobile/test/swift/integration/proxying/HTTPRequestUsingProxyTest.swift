import Envoy
import EnvoyEngine
import EnvoyTestApi
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class HTTPRequestUsingProxyTest: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testHTTPRequestUsingProxy() throws {
    EnvoyTestServer.startHttpProxyServer()
    let port = EnvoyTestServer.getEnvoyPort()

    let engineExpectation = self.expectation(description: "Run started engine")
    let responseHeadersExpectation =
        self.expectation(description: "Successful response headers received")
    let responseTrailersExpectation =
        self.expectation(description: "Successful response trailers received")

    let engine = EngineBuilder()
      .addLogLevel(.trace)
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: false)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    // Send a request to trigger the test filter which should log an event.
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "neverssl.com", path: "/")
      .build()

    var responseBuffer = Data()
    engine.streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         responseHeadersExpectation.fulfill()
      }
      .setOnResponseData { data, _, _ in
        responseBuffer.append(contentsOf: data)
      }
      .setOnResponseTrailers { _, _ in
        responseTrailersExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [responseHeadersExpectation], timeout: 10), .completed)
    XCTAssertEqual(XCTWaiter.wait(for: [responseTrailersExpectation], timeout: 10), .completed)

    if let responseBody = String(data: responseBuffer, encoding: .utf8) {
      XCTAssertGreaterThanOrEqual(responseBody.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }

  func testHTTPSRequestUsingProxy() throws {
    EnvoyTestServer.startHttpsProxyServer()
    let port = EnvoyTestServer.getEnvoyPort()

    let engineExpectation = self.expectation(description: "Run started engine")
    let responseHeadersExpectation =
        self.expectation(description: "Successful response headers received")
    let responseBodyExpectation =
        self.expectation(description: "Successful response trailers received")

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: false)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    // Send a request to trigger the test filter which should log an event.
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "cloud.google.com", path: "/")
      .build()

    var responseBuffer = Data()
    engine.streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         responseHeadersExpectation.fulfill()
      }
      .setOnResponseData { data, endStream, _ in
        responseBuffer.append(contentsOf: data)
        if endStream {
        responseBodyExpectation.fulfill()
        }
      }
      .setOnResponseTrailers { _, _ in
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [responseHeadersExpectation], timeout: 10), .completed)
    XCTAssertEqual(XCTWaiter.wait(for: [responseBodyExpectation], timeout: 10), .completed)

    if let responseBody = String(data: responseBuffer, encoding: .utf8) {
      XCTAssertGreaterThanOrEqual(responseBody.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }

  func testHTTPSRequestUsingPacFileUrlResolver() throws {
    EnvoyTestServer.startHttpsProxyServer()
    let port = EnvoyTestServer.getEnvoyPort()

    let engineExpectation = self.expectation(description: "Run started engine")
    let responseHeadersExpectation =
        self.expectation(description: "Successful response headers received")
    let responseBodyExpectation =
        self.expectation(description: "Successful response trailers received")

    let engine = EngineBuilder()
      .addLogLevel(.debug)
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: true)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    // Send a request to trigger the test filter which should log an event.
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "https",
                                               authority: "cloud.google.com", path: "/")
      .build()

    var responseBuffer = Data()
    engine.streamClient()
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, _, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         responseHeadersExpectation.fulfill()
      }
      .setOnResponseData { data, endStream, _ in
        responseBuffer.append(contentsOf: data)
        if endStream {
        responseBodyExpectation.fulfill()
        }
      }
      .setOnResponseTrailers { _, _ in
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [responseHeadersExpectation], timeout: 10), .completed)
    XCTAssertEqual(XCTWaiter.wait(for: [responseBodyExpectation], timeout: 10), .completed)

    if let responseBody = String(data: responseBuffer, encoding: .utf8) {
      XCTAssertGreaterThanOrEqual(responseBody.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }

  // TODO(abeyad): Add test for proxy system settings updated.
}
