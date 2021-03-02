import Envoy
import Foundation

struct DemoFilter: ResponseFilter {
  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool)
    -> FilterHeadersStatus<ResponseHeaders>
  {
    let builder = headers.toResponseHeadersBuilder()
    builder.add(name: "filter-demo", value: "1")
    return .continue(headers: builder.build())
  }

  func onResponseData(_ body: Data, endStream: Bool) -> FilterDataStatus<ResponseHeaders> {
    // TODO(goaway): Can remove this when we have better integration coverage in place.
    return .continue(data: body)
  }

  func onResponseTrailers(_ trailers: ResponseTrailers)
      -> FilterTrailersStatus<ResponseHeaders, ResponseTrailers> {
    return .continue(trailers: trailers)
  }

  func onError(_ error: EnvoyError) {}

  func onCancel() {}
}
