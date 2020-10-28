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
        case .stopIteration:
          return [kEnvoyFilterHeadersStatusStopIteration, envoyHeaders]
        }
      }

      self.onRequestData = { data, endStream in
        let result = requestFilter.onRequestData(data, endStream: endStream)
        switch result {
        case .continue(let data):
          return [kEnvoyFilterDataStatusContinue, data]
        case .stopIterationAndBuffer:
          return [kEnvoyFilterDataStatusStopIterationAndBuffer, data]
        case .stopIterationNoBuffer:
          return [kEnvoyFilterDataStatusStopIterationNoBuffer, data]
        case .resumeIteration(let headers, let data):
          return [kEnvoyFilterDataStatusResumeIteration, data, headers?.headers as Any]
        }
      }

      self.onRequestTrailers = { envoyTrailers in
        let result = requestFilter.onRequestTrailers(RequestTrailers(headers: envoyTrailers))
        switch result {
        case .continue(let trailers):
          return [kEnvoyFilterTrailersStatusContinue, trailers.headers]
        case .stopIteration:
          return [kEnvoyFilterTrailersStatusStopIteration, envoyTrailers]
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterTrailersStatusResumeIteration,
            trailers.headers,
            headers?.headers as Any,
            data as Any,
          ]
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
        case .stopIteration:
          return [kEnvoyFilterHeadersStatusStopIteration, envoyHeaders]
        }
      }

      self.onResponseData = { data, endStream in
        let result = responseFilter.onResponseData(data, endStream: endStream)
        switch result {
        case .continue(let data):
          return [kEnvoyFilterDataStatusContinue, data]
        case .stopIterationAndBuffer:
          return [kEnvoyFilterDataStatusStopIterationAndBuffer, data]
        case .stopIterationNoBuffer:
          return [kEnvoyFilterDataStatusStopIterationNoBuffer, data]
        case .resumeIteration(let headers, let data):
          return [kEnvoyFilterDataStatusResumeIteration, data, headers?.headers as Any]
        }
      }

      self.onResponseTrailers = { envoyTrailers in
        let result = responseFilter.onResponseTrailers(ResponseTrailers(headers: envoyTrailers))
        switch result {
        case .continue(let trailers):
          return [kEnvoyFilterTrailersStatusContinue, trailers.headers]
        case .stopIteration:
          return [kEnvoyFilterTrailersStatusStopIteration, envoyTrailers]
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterTrailersStatusResumeIteration,
            trailers.headers,
            headers?.headers as Any,
            data as Any,
          ]
        }
      }
    }

    if let asyncRequestFilter = filter as? AsyncRequestFilter {
      self.setRequestFilterCallbacks = { envoyCallbacks in
        asyncRequestFilter.setRequestFilterCallbacks(
          RequestFilterCallbacksImpl(callbacks: envoyCallbacks)
        )
      }

      self.onResumeRequest = { envoyHeaders, data, envoyTrailers, endStream in
        let result = asyncRequestFilter.onResumeRequest(
          headers: envoyHeaders.map(RequestHeaders.init),
          data: data,
          trailers: envoyTrailers.map(RequestTrailers.init),
          endStream: endStream)
        switch result {
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterResumeStatusResumeIteration,
            headers?.headers as Any,
            data as Any,
            trailers?.headers as Any,
          ]
        }
      }
    }

    if let asyncResponseFilter = filter as? AsyncResponseFilter {
      self.setResponseFilterCallbacks = { envoyCallbacks in
        asyncResponseFilter.setResponseFilterCallbacks(
          ResponseFilterCallbacksImpl(callbacks: envoyCallbacks)
        )
      }

      self.onResumeResponse = { envoyHeaders, data, envoyTrailers, endStream in
        let result = asyncResponseFilter.onResumeResponse(
          headers: envoyHeaders.map(ResponseHeaders.init),
          data: data,
          trailers: envoyTrailers.map(ResponseTrailers.init),
          endStream: endStream)
        switch result {
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterResumeStatusResumeIteration,
            headers?.headers as Any,
            data as Any,
            trailers?.headers as Any,
          ]
        }
      }
    }
  }
}
