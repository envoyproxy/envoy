import Envoy
import Foundation

/// Example of a simple HTTP filter that adds a response header.
struct DemoFilter: ResponseFilter {
  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool)
    -> FilterHeadersStatus<ResponseHeaders>
  {
    let builder = headers.toResponseHeadersBuilder()
    builder.add(name: "filter-demo", value: "1")
    return .continue(headers: builder.build())
  }

  func setResponseFilterCallbacks(_ callbacks: ResponseFilterCallbacks) {}

  func onResponseData(_ body: Data, endStream: Bool) -> FilterDataStatus<ResponseHeaders> {
    return .continue(data: body)
  }

  func onResponseTrailers(_ trailers: ResponseTrailers)
      -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
    return .continue(trailers: trailers)
  }

  func onError(_ error: EnvoyError) {}

  func onCancel() {}
}
