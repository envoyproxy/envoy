import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class FilterResetIdleTests: XCTestCase {
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

  func testFilterResetIdle() {
    let filterName = "reset_idle_test_filter"

    class ResetIdleTestFilter: AsyncRequestFilter, ResponseFilter {
      let queue = DispatchQueue(label: "io.envoyproxy.async")
      let resetExpectation: XCTestExpectation
      let cancelExpectation: XCTestExpectation
      var callbacks: RequestFilterCallbacks!
      var resetCount = 0

      // Trigger 3 idle timer exception, then allow timeout
      private func signalActivity() {
        if resetCount < 3 {
          self.resetCount += 1
          self.queue.asyncAfter(deadline: .now() + 0.25) { [weak self] in
            guard let strongSelf = self else {
              return
            }
            strongSelf.callbacks.resetIdleTimer()
            strongSelf.signalActivity()
          }
        } else {
          resetExpectation.fulfill()
        }
      }

      init(resetExpectation: XCTestExpectation, cancelExpectation: XCTestExpectation) {
        self.resetExpectation = resetExpectation
        self.cancelExpectation = cancelExpectation
      }

      func setRequestFilterCallbacks(_ callbacks: RequestFilterCallbacks) {
        self.callbacks = callbacks
      }

      func onResumeRequest(
        headers: RequestHeaders?,
        data: Data?,
        trailers: RequestTrailers?,
        endStream: Bool,
        streamIntel: StreamIntel
      ) -> FilterResumeStatus<RequestHeaders, RequestTrailers> {
        XCTFail("Unexpected call to onResumeRequest")
        return .resumeIteration(headers: nil, data: nil, trailers: nil)
      }

      func onRequestHeaders(_ headers: RequestHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<RequestHeaders>
      {
        self.signalActivity()
        return .stopIteration
      }

      func onRequestData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<RequestHeaders>
      {
        XCTFail("Unexpected call to onRequestData filter callback")
        return .stopIterationNoBuffer
      }

      func onRequestTrailers(_ trailers: RequestTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<RequestHeaders, RequestTrailers>
      {
        XCTFail("Unexpected call to onRequestTrailers filter callback")
        return .stopIteration
      }

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        self.signalActivity()
        return .stopIteration
      }

      func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<ResponseHeaders>
      {
        XCTFail("Unexpected call to onResponseData filter callback")
        return .stopIterationNoBuffer
      }

      func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers>
      {
        XCTFail("Unexpected call to onResponseTrailers filter callback")
        return .stopIteration
      }

      func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel) {}

      func onCancel(streamIntel: FinalStreamIntel) {
        cancelExpectation.fulfill()
      }

      func onComplete(streamIntel: FinalStreamIntel) {}
    }

    let resetExpectation = self.expectation(description: "Stream idle timer reset 3 times")
    let timeoutExpectation = self.expectation(description: "Stream idle timeout triggered")
    let cancelExpectation = self.expectation(
      description: "Stream cancellation triggered incorrectly")
    cancelExpectation.isInverted = true

    EnvoyTestServer.startHttp1PlaintextServer()
    let port = String(EnvoyTestServer.getHttpPort())

    let engine = EngineBuilder()
      .setLogLevel(.debug)
      .setLogger { _, msg in
        print(msg, terminator: "")
      }
      .addStreamIdleTimeoutSeconds(1)
      .addPlatformFilter(
        name: filterName,
        factory: {
          ResetIdleTestFilter(
            resetExpectation: resetExpectation, cancelExpectation: cancelExpectation)
        }
      )
      .build()

    let client = engine.streamClient()

    let requestHeaders = RequestHeadersBuilder(
      method: .get, scheme: "http",
      authority: "localhost:" + port, path: "/test"
    )
    .build()

    client
      .newStreamPrototype()
      .setOnError { _, _ in
        timeoutExpectation.fulfill()
      }
      .start()
      .sendHeaders(requestHeaders, endStream: true)

    XCTAssertEqual(
      XCTWaiter.wait(for: [resetExpectation, timeoutExpectation], timeout: 10),
      .completed
    )

    XCTAssertEqual(
      XCTWaiter.wait(for: [cancelExpectation], timeout: 2),
      .completed
    )

    engine.terminate()
    EnvoyTestServer.shutdownTestHttpServer()
  }
}
