@_implementationOnly import EnvoyEngine

/// Allows for configuring Envoy to return a local response based on matching criteria.
/// Especially useful for testing/mocking clients.
/// https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/
/// v3/route_components.proto#config-route-v3-directresponseaction
public struct DirectResponse {
  public let matcher: RouteMatcher
  public let status: UInt
  public let body: String?
  public let headers: [String: String]

  /// Designated initializer.
  ///
  /// - parameter matcher: The matcher to use for returning a direct response.
  /// - parameter status:  HTTP status code that will be returned with the response.
  /// - parameter body:    String that will be returned as the body of the response.
  /// - parameter headers: Headers to add to the response.
  public init(matcher: RouteMatcher, status: UInt, body: String?, headers: [String: String] = [:]) {
    self.matcher = matcher
    self.status = status
    self.body = body
    self.headers = headers
  }

  func toObjC() -> EMODirectResponse {
    let result = EMODirectResponse()
    result.body = body
    result.status = status
    result.matcher = matcher.toObjC()
    result.headers = headers
    return result
  }
}
