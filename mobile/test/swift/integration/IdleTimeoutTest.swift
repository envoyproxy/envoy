import Envoy
import EnvoyEngine
import EnvoyTestServer
import Foundation
import TestExtensions
import XCTest

final class IdleTimeoutTests: XCTestCase {
  override static func setUp() {
    super.setUp()
    register_test_extensions()
  }

  func testIdleTimeout() {
    let filterName = "reset_idle_test_filter"

    class IdleTimeoutValidationFilter: AsyncResponseFilter, ResponseFilter {
      let timeoutExpectation: XCTestExpectation
      var callbacks: ResponseFilterCallbacks!

      init(timeoutExpectation: XCTestExpectation) {
        self.timeoutExpectation = timeoutExpectation
      }

      func setResponseFilterCallbacks(_ callbacks: ResponseFilterCallbacks) {
        self.callbacks = callbacks
      }

      func onResumeResponse(
        headers: ResponseHeaders?,
        data: Data?,
        trailers: ResponseTrailers?,
        endStream: Bool,
        streamIntel: StreamIntel
      ) -> FilterResumeStatus<ResponseHeaders, ResponseTrailers> {
        XCTFail("Unexpected call to onResumeResponse")
        return .resumeIteration(headers: nil, data: nil, trailers: nil)
      }

      func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
        -> FilterHeadersStatus<ResponseHeaders>
      {
        return .stopIteration
      }

      func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
        -> FilterDataStatus<ResponseHeaders>
      {
        XCTFail("Unexpected call to onResponseData filter callback")
        return .stopIterationNoBuffer
      }

      func onResponseTrailers(_ trailers: ResponseTrailers, streamIntel: StreamIntel)
          -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
        XCTFail("Unexpected call to onResponseTrailers filter callback")
        return .stopIteration
      }

      func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel) {
        XCTAssertEqual(error.errorCode, 4)
        timeoutExpectation.fulfill()
      }

      func onCancel(streamIntel: FinalStreamIntel) {
        XCTFail("Unexpected call to onCancel filter callback")
      }

      func onComplete(streamIntel: FinalStreamIntel) {}
    }

    let filterExpectation = self.expectation(description: "Stream idle timeout received by filter")
    let callbackExpectation =
      self.expectation(description: "Stream idle timeout received by callbacks")

    EnvoyTestServer.startHttp1PlaintextServer()

    let engine = EngineBuilder()
      .addLogLevel(.trace)
      .addStreamIdleTimeoutSeconds(1)
      .addPlatformFilter(
        name: filterName,
        factory: { IdleTimeoutValidationFilter(timeoutExpectation: filterExpectation) }
      )
      .build()

    let client = engine.streamClient()

    let port = String(EnvoyTestServer.getEnvoyPort())
    let requestHeaders = RequestHeadersBuilder(
      method: .get, scheme: "http", authority: "localhost:" + port, path: "/"
    )
    .build()

    client
      .newStreamPrototype()
      .setOnError { error, _ in
        XCTAssertEqual(error.errorCode, 4)
        callbackExpectation.fulfill()
      }
      .setOnCancel { _ in
        XCTFail("Unexpected call to onCancel filter callback")
      }
      .start()
      .sendHeaders(requestHeaders, endStream: false)

    XCTAssertEqual(
      XCTWaiter.wait(for: [filterExpectation, callbackExpectation], timeout: 10),
      .completed
    )

    engine.terminate()
    EnvoyTestServer.shutdownTestServer()
  }
}
