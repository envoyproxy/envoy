import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class GRPCReceiveErrorTests: XCTestCase {
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

    EnvoyTestServer.startHttp1PlaintextServer()

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
          print(msg, terminator: "")
      }
      // The sendLocalReply() call that goes through the error_validation_filter doesn't get
      // triggered unless the idle timeout is less than the connection reset timeout, so the idle
      // timeout is set to be explicitely less than the connection timeout. Plus, the faster
      // timeout makes the test finish faster.
      .addStreamIdleTimeoutSeconds(2)
      .addPlatformFilter(
        name: filterName,
        factory: {
          ErrorValidationFilter(receivedError: filterReceivedError,
                                notCancelled: filterNotCancelled)
        }
      )
      .build()

    let client = Envoy.GRPCClient(streamClient: engine.streamClient())

    let requestHeaders = GRPCRequestHeadersBuilder(
        scheme: "http",
        authority: "localhost:" + String(EnvoyTestServer.getHttpPort()),
        path: "/pb.api.v1.Foo/GetBar")
      .build()
    let message = Data([1, 2, 3, 4, 5])

    client
      .newGRPCStreamPrototype()
      .setOnResponseHeaders { _, _, _ in
        XCTFail("Headers received instead of expected error")
      }
      .setOnResponseMessage { _, _ in
        XCTFail("Message received instead of expected error")
      }
      // The unmatched expecation will cause a local reply which gets translated in Envoy Mobile to
      // an error.
      .setOnError { _, _ in
         callbackReceivedError.fulfill()
      }
      .setOnCancel { _ in
        XCTFail("Unexpected call to onCancel response callback")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)
      .sendMessage(message)

    XCTAssertEqual(XCTWaiter.wait(for: expectations, timeout: 10), .completed)

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}
