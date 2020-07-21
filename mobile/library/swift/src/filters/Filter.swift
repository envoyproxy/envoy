@_implementationOnly import EnvoyEngine
import Foundation

/// Interface representing a filter. See `RequestFilter` and `ResponseFilter` for more details.
public protocol Filter {
  /// A unique name for a filter implementation. Needed for extension registration.
  var name: String { get }
}

extension EnvoyHTTPFilter {
  /// Initialize an EnvoyHTTPFilter using the instance methods of a concrete Filter implementation.
  convenience init(filter: Filter) {
    self.init()
    self.name = filter.name

    if let requestFilter = filter as? RequestFilter {
      self.onRequestHeaders = { envoyHeaders, endStream in
        let result = requestFilter.onRequestHeaders(RequestHeaders(headers: envoyHeaders),
                                                    endStream: endStream)
        switch result {
        case .continue(let headers):
          return [kEnvoyFilterHeadersStatusContinue, headers.headers]
        case .stopIteration(let headers):
          return [kEnvoyFilterHeadersStatusStopIteration, headers.headers]
        }
      }
    }

    if let responseFilter = filter as? ResponseFilter {
      self.onResponseHeaders = { envoyHeaders, endStream in
        let result = responseFilter.onResponseHeaders(ResponseHeaders(headers: envoyHeaders),
                                                      endStream: endStream)
        switch result {
        case .continue(let headers):
          return [kEnvoyFilterHeadersStatusContinue, headers.headers]
        case .stopIteration(let headers):
          return [kEnvoyFilterHeadersStatusStopIteration, headers.headers]
        }
      }
    }
  }
}
