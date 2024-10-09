import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class SendHeadersTests: XCTestCase {
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

  func testSendHeaders() {
    EnvoyTestServer.startHttp1PlaintextServer()

    let headersExpectation = self.expectation(
      description: "Run called with expected http headers status")
    let endStreamExpectation = self.expectation(description: "End stream encountered")
    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .build()

    let client = engine.streamClient()

    let port = String(EnvoyTestServer.getHttpPort())
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/simple.txt")
      .build()

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         headersExpectation.fulfill()
         if endStream {
           endStreamExpectation.fulfill()
         }
      }
      .setOnResponseData { _, endStream, _ in
         if endStream {
           endStreamExpectation.fulfill()
         }
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    let expectations = [headersExpectation, endStreamExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}
