import Envoy
import Foundation

struct DemoFilter: ResponseFilter {
  // TODO(goaway): Update once dynamic registration is in place.
  let name = "PlatformStub"

  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool)
    -> FilterHeaderStatus<ResponseHeaders>
  {
    NSLog("Adding new header!")
    let builder = headers.toResponseHeadersBuilder()
    builder.add(name: "filter-demo", value: "1")
    return .continue(builder.build())
  }

  func setResponseFilterCallbacks(_ callbacks: ResponseFilterCallbacks) {}

  func onResponseData(_ body: Data, endStream: Bool) -> FilterDataStatus {
    return .continue(body)
  }

  func onResponseTrailers(_ trailers: ResponseTrailers) -> FilterTrailerStatus<ResponseTrailers> {
    return .continue(trailers)
  }
}
