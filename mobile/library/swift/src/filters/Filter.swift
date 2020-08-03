@_implementationOnly import EnvoyEngine
import Foundation

/// Interface representing a filter. See `RequestFilter` and `ResponseFilter` for more details.
public protocol Filter {
}

extension EnvoyHTTPFilterFactory {
  convenience init(filterName: String, factory: @escaping () -> Filter) {
    self.init()
    self.filterName = filterName
    self.create = { EnvoyHTTPFilter(filter: factory()) }
  }
}

extension EnvoyHTTPFilter {
  /// Initialize an EnvoyHTTPFilter using the instance methods of a concrete Filter implementation.
  convenience init(filter: Filter) {
    self.init()

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

      self.onRequestData = { data, endStream in
        let result = requestFilter.onRequestData(data, endStream: endStream)
        switch result {
        case .continue(let data):
          return [kEnvoyFilterDataStatusContinue, data]
        case .stopIterationAndBuffer(let data):
          return [kEnvoyFilterDataStatusStopIterationAndBuffer, data]
        case .stopIterationNoBuffer:
          // TODO(goaway): this probably shouldn't return any associated data, but bridge code
          // would need to be updated to handle nil. Alternatively, when an unmodified flag is
          // added, that could suffice here, too.
          return [kEnvoyFilterDataStatusStopIterationNoBuffer, data]
        }
      }

      self.onRequestTrailers = { envoyTrailers in
        let result = requestFilter.onRequestTrailers(RequestTrailers(headers: envoyTrailers))
        switch result {
        case .continue(let trailers):
          return [kEnvoyFilterTrailersStatusContinue, trailers.headers]
        case .stopIteration(let trailers):
          return [kEnvoyFilterTrailersStatusStopIteration, trailers.headers]
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

      self.onResponseData = { data, endStream in
        let result = responseFilter.onResponseData(data, endStream: endStream)
        switch result {
        case .continue(let data):
          return [kEnvoyFilterDataStatusContinue, data]
        case .stopIterationAndBuffer(let data):
          return [kEnvoyFilterDataStatusStopIterationAndBuffer, data]
        case .stopIterationNoBuffer:
          // TODO(goaway): this probably shouldn't return any associated data, but bridge code
          // would need to be updated to handle nil. Alternatively, when an unmodified flag is
          // added, that could suffice here, too.
          return [kEnvoyFilterDataStatusStopIterationNoBuffer, data]
        }
      }

      self.onResponseTrailers = { envoyTrailers in
        let result = responseFilter.onResponseTrailers(ResponseTrailers(headers: envoyTrailers))
        switch result {
        case .continue(let trailers):
          return [kEnvoyFilterTrailersStatusContinue, trailers.headers]
        case .stopIteration(let trailers):
          return [kEnvoyFilterTrailersStatusStopIteration, trailers.headers]
        }
      }
    }
  }
}
