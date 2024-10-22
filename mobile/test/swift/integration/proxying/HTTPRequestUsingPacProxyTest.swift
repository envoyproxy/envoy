import Envoy
import EnvoyEngine
import EnvoyTestApi
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class HTTPRequestUsingPacProxyTest: XCTestCase {
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

  private func executeRequest(engine: Engine, scheme: String, authority: String, path: String)
    -> String? {
    let responseHeadersExpectation =
        self.expectation(description: "Successful response headers received")
    let responseTrailersExpectation =
        self.expectation(description: "Successful response trailers received")

    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: scheme,
                                               authority: authority, path: path)
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

  // TODO(abeyad): Right now, this test doesn't correctly execute
  // `CFNetworkExecuteProxyAutoConfigurationURL`, so it doesn't go through the proxy, but it proves
  // that PAC proxy resolution callbacks and the threading works correctly. Once the issue with
  // testing `CFNetworkExecuteProxyAutoConfigurationURL` is resolved, add a check to verify that
  // the request went through the actual configured proxy.
  func testHTTPRequestUsingPacProxy() throws {
    EnvoyTestServer.startHttpProxyServer()
    let proxyPort = EnvoyTestServer.getProxyPort()
    EnvoyTestServer.startHttp1PlaintextServer()
    let httpPort = EnvoyTestServer.getHttpPort()

    let engineExpectation = self.expectation(description: "Run started engine")

    let engine = EngineBuilder()
      .setLogLevel(.info)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .setOnEngineRunning {
        engineExpectation.fulfill()
      }
      .respectSystemProxySettings(true)
      .build()

    let pacScript = """
    function FindProxyForURL(url, host) {
      return "PROXY 127.0.0.1:\(proxyPort)";
    }
    """
    EnvoyTestServer.setHeadersAndData("Content-Type",
                                      header_value: "application/x-ns-proxy-autoconfig",
                                      response_body: pacScript)
    EnvoyTestApi.registerTestProxyResolver("127.0.0.1", port: httpPort, usePacResolver: true)

    XCTAssertEqual(XCTWaiter.wait(for: [engineExpectation], timeout: 5), .completed)

    let host = "www.gstatic.com"
    let path = "/ytlr/txt/licenses_cobalt.txt"
    if let respBody = executeRequest(engine: engine, scheme: "http", authority: host, path: path) {
      XCTAssertGreaterThanOrEqual(respBody.utf8.count, 5000)
    }

    engine.terminate()
    EnvoyTestServer.shutdownTestProxyServer()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}
