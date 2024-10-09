import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class CancelStreamTests: XCTestCase {
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

  func testCancelStream() {
    let filterName = "cancel_validation_filter"

    struct CancelValidationFilter: ResponseFilter {
      let expectation: XCTestExpectation

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        return .continue(headers: headers)
      }

      func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<ResponseHeaders>
      {
        return .continue(data: body)
      }

      func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
        return .continue(trailers: trailers)
      }

      func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel) {}

      func onCancel(streamIntel: FinalStreamIntel) {
        self.expectation.fulfill()
      }

      func onComplete(streamIntel: FinalStreamIntel) {}
    }

    let runExpectation = self.expectation(description: "Run called with expected cancellation")
    let filterExpectation = self.expectation(description: "Filter called with cancellation")

    EnvoyTestServer.startHttp1PlaintextServer()

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .addPlatformFilter(
        name: filterName,
        factory: { CancelValidationFilter(expectation: filterExpectation) }
      )
      .build()

    let client = engine.streamClient()

    let port = String(EnvoyTestServer.getHttpPort())
    let requestHeaders = RequestHeadersBuilder(method: .get, scheme: "http",
                                               authority: "localhost:" + port, path: "/")
      .build()

    client
      .newStreamPrototype()
      .setOnCancel { _ in
         runExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .cancel()

    let expectations = [filterExpectation, runExpectation]
    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}
