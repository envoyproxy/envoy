import Foundation

/// Allows for configuring Envoy to return a local response based on matching criteria.
/// Especially useful for testing/mocking clients.
/// https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/
/// v3/route_components.proto#config-route-v3-directresponseaction
@objcMembers
public final class DirectResponse: NSObject {
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
    super.init()
  }

  /// - returns: YAML that can be used for route matching in Envoy configurations.
  func resolvedRouteMatchYAML() -> String {
    return
      """
      \(self.resolvedMatchYAML())
                        route: { cluster: fake_remote }
      """
  }

  /// - returns: YAML that can be used for route matching & direct responses
  ///            in Envoy configurations.
  func resolvedDirectResponseYAML() -> String {
    let formattedResponseHeaders = self.headers.map { name, value in
      """
                          - header:
                              key: "\(name)"
                              value: "\(value)"
      """
    }.joined(separator: "\n")

    return
      """
      \(self.resolvedMatchYAML())
                        direct_response:
                          status: \(self.status)
                          body: \(self.body.map { "{ inline_string: '\($0)' }" } ?? "")
                        response_headers_to_add:
      \(formattedResponseHeaders)
      """
  }

  private func resolvedMatchYAML() -> String {
    let pathMatch: String
    if let fullPath = self.matcher.fullPath {
      pathMatch = "path: \"\(fullPath)\""
    } else if let pathPrefix = self.matcher.pathPrefix {
      pathMatch = "prefix: \"\(pathPrefix)\""
    } else {
      // Ideally we could use an enum with associated values to simplify this into
      // a single initializer and enforce this at compile time, but it is not
      // compatible with Objective-C.
      preconditionFailure("Unexpectedly allowed DirectResponse with no path matches")
    }

    let formattedHeaderMatches = self.matcher.headers.map { header in
      """
                          headers:
                            - name: "\(header.name)"
                              \(header.mode.resolvedYAML(value: header.value))
      """
    }.joined(separator: "\n")

    return
      """
                      - match:
                          \(pathMatch)
      \(formattedHeaderMatches)
      """
  }
}
