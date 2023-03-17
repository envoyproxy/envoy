import Foundation

/// Typed representation of a route matcher that may be specified when starting the engine.
/// https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/
/// v3/route_components.proto#envoy-v3-api-msg-config-route-v3-routematch
@objcMembers
public final class RouteMatcher: NSObject {
  public let fullPath: String?
  public let pathPrefix: String?
  public let headers: [HeaderMatcher]

  @objcMembers
  public final class HeaderMatcher: NSObject {
    public let name: String
    public let value: String
    public let mode: MatchMode

    @objc
    public enum MatchMode: Int {
      case contains
      case exact
      case prefix
      case suffix

      func resolvedYAML(value: String) -> String {
        switch self {
        case .contains:
          return "contains_match: \"\(value)\""
        case .exact:
          return "exact_match: \"\(value)\""
        case .prefix:
          return "prefix_match: \"\(value)\""
        case .suffix:
          return "suffix_match: \"\(value)\""
        }
      }
    }

    public init(name: String, value: String, mode: MatchMode) {
      self.name = name
      self.value = value
      self.mode = mode
      super.init()
    }
  }

  /// Initialize a matcher with a path prefix.
  ///
  /// - parameter pathPrefix: This prefix must match the beginning of the :path header.
  /// - parameter headers:    Specifies a set of headers that the route should match on. The
  ///                         router will check the request’s headers against all the specified
  ///                         headers in the route
  ///                         config. A match will happen if all the headers in the route are
  ///                         present in the
  ///                         request with the same values (or based on presence if the value
  ///                         field is not in the config).
  public init(pathPrefix: String, headers: [HeaderMatcher] = []) {
    self.fullPath = nil
    self.pathPrefix = pathPrefix
    self.headers = headers
    super.init()
  }

  /// Initialize a matcher with a full path.
  ///
  /// - parameter fullPath: This value must exactly match the :path header once the query string is
  ///                       removed.
  /// - parameter headers:  Specifies a set of headers that the route should match on. The
  ///                       router will check the request’s headers against all the specified
  ///                       headers in the route
  ///                       config. A match will happen if all the headers in the route are
  ///                       present in the
  ///                       request with the same values (or based on presence if the value
  ///                       field is not in the config).
  public init(fullPath: String, headers: [HeaderMatcher] = []) {
    self.fullPath = fullPath
    self.pathPrefix = nil
    self.headers = headers
    super.init()
  }
}
