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

  // https://github.com/envoyproxy/envoy/issues/33014
  func skipped_testHTTPRequestUsingProxy() throws {
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

    let expectations = [responseHeadersExpectation, responseTrailersExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    if let responseBody = String(data: responseBuffer, encoding: .utf8) {
      XCTAssertGreaterThanOrEqual(responseBody.utf8.count, 3900)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }

  // https://github.com/envoyproxy/envoy/issues/33014
  func skipped_testHTTPSRequestUsingProxy() throws {
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
    EnvoyTestServer.shutdownTestServer()
  }

  // https://github.com/envoyproxy/envoy/issues/33014
  func skipped_testHTTPSRequestUsingPacFileUrlResolver() throws {
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
    EnvoyTestServer.shutdownTestServer()
  }

  // https://github.com/envoyproxy/envoy/issues/33014
  func skipped_testHTTPRequestUsingProxyCancelStream() throws {
    EnvoyTestServer.startHttpProxyServer()
    let port = EnvoyTestServer.getEnvoyPort()

    let engineExpectation = self.expectation(description: "Run started engine")

    let engine = EngineBuilder()
      .addLogLevel(.trace)
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
    EnvoyTestServer.shutdownTestServer()
  }

  // TODO(abeyad): Add test for proxy system settings updated.
}
