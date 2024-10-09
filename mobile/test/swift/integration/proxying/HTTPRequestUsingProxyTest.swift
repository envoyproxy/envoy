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

  override static func tearDown() {
    super.tearDown()
    // Flush the stdout and stderror to show the print output.
    fflush(stdout)
    fflush(stderr)
  }

  private func executeRequest(engine: Engine, scheme: String, authority: String) -> String? {
    let responseHeadersExpectation =
        self.expectation(description: "Successful response headers received")
    let responseTrailersExpectation =
        self.expectation(description: "Successful response trailers received")

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: scheme,
                                               authority: authority, path: "/")
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

    let expectations = [responseHeadersExpectation, responseTrailersExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    return String(data: responseBuffer, encoding: .utf8)
  }

  func testHTTPRequestUsingProxy() throws {
    EnvoyTestServer.startHttpProxyServer()
    let port = EnvoyTestServer.getProxyPort()

    let engineExpectation = self.expectation(description: "Run started engine")

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: false)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    if let respBody = executeRequest(engine: engine, scheme: "http", authority: "neverssl.com") {
      XCTAssertGreaterThanOrEqual(respBody.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestProxyServer()
  }

  // https://github.com/envoyproxy/envoy/issues/33014
  func skipped_testHTTPSRequestUsingProxy() throws {
    EnvoyTestServer.startHttpsProxyServer()
    let port = EnvoyTestServer.getProxyPort()

    let engineExpectation = self.expectation(description: "Run started engine")
    let responseHeadersExpectation =
        self.expectation(description: "Successful response headers received")
    let responseBodyExpectation =
        self.expectation(description: "Successful response trailers received")

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: false)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

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

    let expectations = [responseHeadersExpectation, responseBodyExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    if let responseBody = String(data: responseBuffer, encoding: .utf8) {
      XCTAssertGreaterThanOrEqual(responseBody.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestProxyServer()
  }

  // https://github.com/envoyproxy/envoy/issues/33014
  func skipped_testHTTPSRequestUsingPacFileUrlResolver() throws {
    EnvoyTestServer.startHttpsProxyServer()
    let port = EnvoyTestServer.getProxyPort()

    let engineExpectation = self.expectation(description: "Run started engine")
    let responseHeadersExpectation =
        self.expectation(description: "Successful response headers received")
    let responseBodyExpectation =
        self.expectation(description: "Successful response trailers received")

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: true)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

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

    let expectations = [responseHeadersExpectation, responseBodyExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    if let responseBody = String(data: responseBuffer, encoding: .utf8) {
      XCTAssertGreaterThanOrEqual(responseBody.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestProxyServer()
  }

  func testTwoHTTPRequestsUsingProxy() throws {
    EnvoyTestServer.startHttpProxyServer()
    let port = EnvoyTestServer.getProxyPort()

    let engineExpectation = self.expectation(description: "Run started engine")

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: false)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    if let resp1 = executeRequest(engine: engine, scheme: "http", authority: "neverssl.com") {
      XCTAssertGreaterThanOrEqual(resp1.utf8.count, 3900)
    }
    if let resp2 = executeRequest(engine: engine, scheme: "http", authority: "neverssl.com") {
      XCTAssertGreaterThanOrEqual(resp2.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestProxyServer()
  }

  func testHTTPRequestUsingProxyCancelStream() throws {
    EnvoyTestServer.startHttpProxyServer()
    let port = EnvoyTestServer.getProxyPort()

    let engineExpectation = self.expectation(description: "Run started engine")

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: port, usePacResolver: false)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "neverssl.com", path: "/")
      .build()

    let cancelExpectation = self.expectation(description: "Stream run with expected cancellation")

    engine.streamClient()
      .newStreamPrototype()
      .setOnCancel { _ in
         // Handle stream cancellation, which is expected since we immediately
         // cancel the stream after sending headers on it.
         cancelExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)
      .cancel()

    XCTAssertEqual(XCTWaiter.wait(for: [cancelExpectation], timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestProxyServer()
  }

  // TODO(abeyad): Add test for proxy system settings updated.
}
