import Envoy
import Foundation

struct DemoFilter: ResponseFilter {
  // TODO(goaway): Update once dynamic registration is in place.
  static let name = "PlatformStub"

  func onResponseHeaders(_ headers: ResponseHeaders, endStream: Bool)
    -> FilterHeadersStatus<ResponseHeaders>
  {
    NSLog("Adding new header!")
    let builder = headers.toResponseHeadersBuilder()
    builder.add(name: "filter-demo", value: "1")
    return .continue(builder.build())
  }

  func setResponseFilterCallbacks(_ callbacks: ResponseFilterCallbacks) {}

  func onResponseData(_ body: Data, endStream: Bool) -> FilterDataStatus {
    // TODO(goaway): Can remove this when we have better integration coverage in place.
    NSLog("Saw data chunk of length \(body.count)")
    return .continue(body)
  }

  func onResponseTrailers(_ trailers: ResponseTrailers) -> FilterTrailersStatus<ResponseTrailers> {
    return .continue(trailers)
  }
}
