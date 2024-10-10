import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class ResetConnectivityStateTest: XCTestCase {
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

  func testResetConnectivityState() {
    EnvoyTestServer.startHttp1PlaintextServer()

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

    let expectation1 =
      self.expectation(description: "Run called with expected http status first request")
    let finish1 = self.expectation(description: "endStream sent to callback")
    var resultEndStream1: Bool = false

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         resultEndStream1 = endStream
         expectation1.fulfill()
         if endStream {
          finish1.fulfill()
         }
      }
      .setOnResponseData { _, endStream, _ in
         resultEndStream1 = endStream
         if endStream {
          finish1.fulfill()
         }
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation1, finish1], timeout: 10), .completed)
    XCTAssertTrue(resultEndStream1)

    engine.resetConnectivityState()

    let expectation2 =
      self.expectation(description: "Run called with expected http status first request")
    let finish2 = self.expectation(description: "endStream sent to callback")
    var resultEndStream2: Bool = false

    client
      .newStreamPrototype()
      .setOnResponseHeaders { responseHeaders, endStream, _ in
         XCTAssertEqual(200, responseHeaders.httpStatus)
         resultEndStream2 = endStream
         expectation2.fulfill()
         if endStream {
          finish2.fulfill()
         }
      }
      .setOnResponseData { _, endStream, _ in
         resultEndStream2 = endStream
         if endStream {
          finish2.fulfill()
         }
      }
      .setOnError { _, _ in
        XCTFail("Unexpected error")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: [expectation2, finish2], timeout: 10), .completed)
    XCTAssertTrue(resultEndStream2)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}
