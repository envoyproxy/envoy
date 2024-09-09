import Envoy
import EnvoyEngine
import Foundation
import TestExtensions
import XCTest

final class ReceiveErrorTests: XCTestCase {
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

  func testReceiveError() {
    let filterName = "error_validation_filter"

    struct ErrorValidationFilter: ResponseFilter {
      let receivedError: XCTestExpectation
      let notCancelled: XCTestExpectation

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

      func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel) {
        XCTAssertEqual(error.errorCode, 2) // 503/Connection Failure
        self.receivedError.fulfill()
      }

      func onCancel(streamIntel: FinalStreamIntel) {
        XCTFail("Unexpected call to onCancel filter callback")
        self.notCancelled.fulfill()
      }

      func onComplete(streamIntel: FinalStreamIntel) {}
    }

    let callbackReceivedError = self.expectation(description: "Run called with expected error")
    let filterReceivedError = self.expectation(description: "Filter called with expected error")
    let filterNotCancelled =
      self.expectation(description: "Filter called with unexpected cancellation")
    filterNotCancelled.isInverted = true
    let expectations = [filterReceivedError, filterNotCancelled, callbackReceivedError]

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .addPlatformFilter(
        name: filterName,
        factory: {
          ErrorValidationFilter(receivedError: filterReceivedError,
                                notCancelled: filterNotCancelled)
        }
      )
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(
        method: .get,
        scheme: "https",
        authority: "doesnotexist.example.com",
        path: "/test")
      .build()

    client
      .newStreamPrototype()
      .setOnResponseHeaders { _, _, _ in
        XCTFail("Headers received instead of expected error")
      }
      .setOnResponseData { _, _, _ in
        XCTFail("Data received instead of expected error")
      }
      // The unmatched expectation will cause a local reply which gets translated in Envoy Mobile to
      // an error.
      .setOnError { error, _ in
         XCTAssertEqual(error.errorCode, 2) // 503/Connection Failure
         callbackReceivedError.fulfill()
      }
      .setOnCancel { _ in
        XCTFail("Unexpected call to onCancel response callback")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    engine.terminate()
  }
}
