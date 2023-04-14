@_implementationOnly import EnvoyEngine

/// Typed representation of a route matcher that may be specified when starting the engine.
/// https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/
/// v3/route_components.proto#envoy-v3-api-msg-config-route-v3-routematch
public struct RouteMatcher {
  public let fullPath: String?
  public let pathPrefix: String?
  public let headers: [HeaderMatcher]

  public struct HeaderMatcher {
    public let name: String
    public let value: String
    public let mode: MatchMode

    public enum MatchMode: Int {
      case contains
      case exact
      case prefix
      case suffix

      fileprivate var objcValue: EMOMatchMode {
        switch self {
        case .contains:
          return .contains
        case .exact:
          return .exact
        case .prefix:
          return .prefix
        case .suffix:
          return .suffix
        }
      }
    }

    public init(name: String, value: String, mode: MatchMode) {
      self.name = name
      self.value = value
      self.mode = mode
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
  }
}

extension RouteMatcher {
  func toObjC() -> EMORouteMatcher {
    let result = EMORouteMatcher()
    result.fullPath = fullPath
    result.pathPrefix = pathPrefix
    result.headers = headers.map { $0.toObjC() }
    return result
  }
}

private extension RouteMatcher.HeaderMatcher {
  func toObjC() -> EMOHeaderMatcher {
    let result = EMOHeaderMatcher()
    result.name = name
    result.value = value
    result.mode = mode.objcValue
    return result
  }
}
