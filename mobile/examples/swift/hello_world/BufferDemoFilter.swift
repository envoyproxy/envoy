import Envoy
import Foundation

/// Example of a more complex HTTP filter that pauses processing on the response filter chain,
/// buffers until the response is complete, then resumes filter iteration while setting a new
/// header.
final class BufferDemoFilter: ResponseFilter {
  private var headers: ResponseHeaders!
  private var body: Data?

  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool, streamIntel: StreamIntel)
    -> FilterHeadersStatus<ResponseHeaders>
  {
    self.headers = headers
    return .stopIteration
  }

  func onResponseData(_ data: Data, endStream: Bool, streamIntel: StreamIntel)
    -> FilterDataStatus<ResponseHeaders>
  {
    // Since we request buffering, each invocation will include all data buffered so far.
    self.body = data

    // If this is the end of the stream, resume processing of the (now fully-buffered) response.
    if endStream {
      let builder = self.headers.toResponseHeadersBuilder()
        .add(name: "buffer-filter-demo", value: "1")
      return .resumeIteration(headers: builder.build(), data: data)
    }
    return .stopIterationAndBuffer
  }

  func onResponseTrailers(
    _ trailers: ResponseTrailers,
    streamIntel: StreamIntel
  ) -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
    // Trailers imply end of stream; resume processing of the (now fully-buffered) response.
    let builder = self.headers.toResponseHeadersBuilder()
      .add(name: "buffer-filter-demo", value: "1")
    return .resumeIteration(headers: builder.build(), data: self.body, trailers: trailers)
  }

  func onError(_ error: EnvoyError, streamIntel: StreamIntel) {}

  func onCancel(streamIntel: StreamIntel) {}
}
