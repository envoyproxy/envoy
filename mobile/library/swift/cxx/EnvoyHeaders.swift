@_implementationOnly import EnvoyCxxSwiftInterop

typealias EnvoyHeaders = [String: [String]]

func toSwiftHeaders(_ headers: envoy_headers) -> EnvoyHeaders {
  defer {
    release_envoy_headers(headers)
  }

  return (0..<Int(headers.length)).reduce(into: EnvoyHeaders()) { result, i in
    let header = headers.entries[i]
    if
      let headerKey = String.fromEnvoyData(header.key),
      let headerValue = String.fromEnvoyData(header.value)
    {
      result[headerKey, default: []].append(headerValue)
    }
  }
}

func toEnvoyHeaders(_ headers: EnvoyHeaders?) -> envoy_headers {
  guard let headers else {
    return envoy_noheaders
  }

  var rawHeaderMap = Envoy.Platform.RawHeaderMap()
  for (key, values) in headers {
    Envoy.CxxSwift.raw_header_map_set(&rawHeaderMap, key.toCXX(), values.toCXX())
  }
  return Envoy.Platform.rawHeaderMapAsEnvoyHeaders(rawHeaderMap)
}

func toEnvoyHeadersPtr(_ headers: EnvoyHeaders?) -> UnsafeMutablePointer<envoy_headers>? {
  guard let headers else {
    return nil
  }

  let ptr = UnsafeMutablePointer<envoy_headers>.allocate(capacity: 1)
  var envoyHeaders = toEnvoyHeaders(headers)
  ptr.assign(from: &envoyHeaders, count: 1)
  return ptr
}
