import Envoy
import Foundation

/// Example of a more complex HTTP filter that pauses processing on the response filter chain,
/// buffers until the response is complete, then asynchronously triggers filter chain resumption
/// while setting a new header. Also demonstrates safety of re-entrancy of async callbacks.
final class AsyncDemoFilter: AsyncResponseFilter {
  private var callbacks: ResponseFilterCallbacks!

  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
    -> FilterHeadersStatus<ResponseHeaders>
  {
    if endStream {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in
        self?.callbacks.resumeResponse()
      }
    }
    return .stopIteration
  }

  func onResponseData(_ body: Data, endStream: Bool, streamIntel: StreamIntel)
    -> FilterDataStatus<ResponseHeaders>
  {
    if endStream {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in
        self?.callbacks.resumeResponse()
      }
    }
    return .stopIterationAndBuffer
  }

  func onResponseTrailers(
    _ trailers: ResponseTrailers,
    streamIntel: StreamIntel
  ) -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in
      self?.callbacks.resumeResponse()
    }
    return .stopIteration
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
    guard let headers = headers else {
      // Iteration was stopped on headers, so headers must be present.
      fatalError("Filter behavior violation!")
    }
    let builder = headers.toResponseHeadersBuilder()
      .add(name: "async-filter-demo", value: "1")
    return .resumeIteration(headers: builder.build(), data: data, trailers: trailers)
  }

  func onError(_ error: EnvoyError, streamIntel: FinalStreamIntel) {}

  func onCancel(streamIntel: FinalStreamIntel) {}

  func onComplete(streamIntel: FinalStreamIntel) {}
}
