@_implementationOnly import EnvoyCxxSwiftInterop

// MARK: - Internal

extension DirectResponse {
  func toCXX() -> Envoy.DirectResponseTesting.DirectResponse {
    var result = Envoy.DirectResponseTesting.DirectResponse()
    result.status = UInt32(self.status)
    if let body = self.body {
      result.body = body.toCXX()
    }
    result.matcher = self.matcher.toCXX()
    result.headers = self.headers.toCXX()
    return result
  }
}

// MARK: - Private

private extension RouteMatcher {
  func toCXX() -> Envoy.DirectResponseTesting.RouteMatcher {
    var result = Envoy.DirectResponseTesting.RouteMatcher()
    if let fullPath = self.fullPath {
      result.fullPath = fullPath.toCXX()
    }
    if let pathPrefix = self.pathPrefix {
      result.pathPrefix = pathPrefix.toCXX()
    }
    result.headers = self.headers.toCXX()
    return result
  }
}

private extension RouteMatcher.HeaderMatcher {
  func toCXX() -> Envoy.DirectResponseTesting.HeaderMatcher {
    return Envoy.DirectResponseTesting.HeaderMatcher(
      name: self.name.toCXX(),
      value: self.value.toCXX(),
      mode: self.mode.toCXX()
    )
  }
}

private extension Array<RouteMatcher.HeaderMatcher> {
  func toCXX() -> Envoy.CxxSwift.HeaderMatcherVector {
    var vector = Envoy.CxxSwift.HeaderMatcherVector()
    for element in self {
      var cppElement = element.toCXX()
      vector.push_back(&cppElement)
    }
    return vector
  }
}

private extension RouteMatcher.HeaderMatcher.MatchMode {
  func toCXX() -> Envoy.DirectResponseTesting.MatchMode {
    switch self {
    case .contains:
      return .Contains
    case .exact:
      return .Exact
    case .prefix:
      return .Prefix
    case .suffix:
      return .Suffix
    }
  }
}
