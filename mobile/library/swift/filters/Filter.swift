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
  ///
  /// - parameter filter: The contrete `Filter` to wrap.
  convenience init(filter: Filter) {
    self.init()

    if let requestFilter = filter as? RequestFilter {
      self.onRequestHeaders = { envoyHeaders, endStream, streamIntel in
        let result = requestFilter.onRequestHeaders(RequestHeaders(headers: envoyHeaders),
                                                    endStream: endStream,
                                                    streamIntel: StreamIntel(streamIntel))
        switch result {
        case .continue(let headers):
          return [kEnvoyFilterHeadersStatusContinue, headers.caseSensitiveHeaders()]
        case .stopIteration:
          return [kEnvoyFilterHeadersStatusStopIteration, NSNull()]
        }
      }

      self.onRequestData = { data, endStream, streamIntel in
        let result = requestFilter.onRequestData(data, endStream: endStream,
                                                 streamIntel: StreamIntel(streamIntel))
        switch result {
        case .continue(let data):
          return [kEnvoyFilterDataStatusContinue, data]
        case .stopIterationAndBuffer:
          return [kEnvoyFilterDataStatusStopIterationAndBuffer, NSNull()]
        case .stopIterationNoBuffer:
          return [kEnvoyFilterDataStatusStopIterationNoBuffer, NSNull()]
        case .resumeIteration(let headers, let data):
          return [
                    kEnvoyFilterDataStatusResumeIteration, data,
                    headers?.caseSensitiveHeaders() as Any,
          ]
        }
      }

      self.onRequestTrailers = { envoyTrailers, streamIntel in
        let result = requestFilter.onRequestTrailers(RequestTrailers(headers: envoyTrailers),
                                                     streamIntel: StreamIntel(streamIntel))
        switch result {
        case .continue(let trailers):
          return [kEnvoyFilterTrailersStatusContinue, trailers.caseSensitiveHeaders()]
        case .stopIteration:
          return [kEnvoyFilterTrailersStatusStopIteration, NSNull()]
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterTrailersStatusResumeIteration,
            trailers.caseSensitiveHeaders(),
            headers?.caseSensitiveHeaders() as Any,
            data as Any,
          ]
        }
      }
    }

    if let responseFilter = filter as? ResponseFilter {
      self.onResponseHeaders = { envoyHeaders, endStream, streamIntel in
        let result = responseFilter.onResponseHeaders(ResponseHeaders(headers: envoyHeaders),
                                                      endStream: endStream,
                                                      streamIntel: StreamIntel(streamIntel))
        switch result {
        case .continue(let headers):
          return [kEnvoyFilterHeadersStatusContinue, headers.caseSensitiveHeaders()]
        case .stopIteration:
          return [kEnvoyFilterHeadersStatusStopIteration, NSNull()]
        }
      }

      self.onResponseData = { data, endStream, streamIntel in
        let result = responseFilter.onResponseData(data, endStream: endStream,
                                                   streamIntel: StreamIntel(streamIntel))
        switch result {
        case .continue(let data):
          return [kEnvoyFilterDataStatusContinue, data]
        case .stopIterationAndBuffer:
          return [kEnvoyFilterDataStatusStopIterationAndBuffer, NSNull()]
        case .stopIterationNoBuffer:
          return [kEnvoyFilterDataStatusStopIterationNoBuffer, NSNull()]
        case .resumeIteration(let headers, let data):
          return [
                    kEnvoyFilterDataStatusResumeIteration, data,
                    headers?.caseSensitiveHeaders() as Any,
          ]
        }
      }

      self.onResponseTrailers = { envoyTrailers, streamIntel in
        let result = responseFilter.onResponseTrailers(ResponseTrailers(headers: envoyTrailers),
                                                       streamIntel: StreamIntel(streamIntel))
        switch result {
        case .continue(let trailers):
          return [kEnvoyFilterTrailersStatusContinue, trailers.caseSensitiveHeaders()]
        case .stopIteration:
          return [kEnvoyFilterTrailersStatusStopIteration, NSNull()]
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterTrailersStatusResumeIteration,
            trailers.caseSensitiveHeaders(),
            headers?.caseSensitiveHeaders() as Any,
            data as Any,
          ]
        }
      }

      self.onError = { errorCode, message, attemptCount, streamIntel, finalStreamIntel in
        let error = EnvoyError(errorCode: errorCode, message: message,
                               attemptCount: UInt32(exactly: attemptCount), cause: nil)
        responseFilter.onError(error, streamIntel: FinalStreamIntel(streamIntel, finalStreamIntel))
      }

      self.onCancel = { streamIntel, finalStreamIntel in
        responseFilter.onCancel(streamIntel: FinalStreamIntel(streamIntel, finalStreamIntel))
      }

      self.onComplete = { streamIntel, finalStreamIntel in
        responseFilter.onComplete(streamIntel: FinalStreamIntel(streamIntel, finalStreamIntel))
      }
    }

    if let asyncRequestFilter = filter as? AsyncRequestFilter {
      self.setRequestFilterCallbacks = { envoyCallbacks in
        asyncRequestFilter.setRequestFilterCallbacks(
          RequestFilterCallbacksImpl(callbacks: envoyCallbacks)
        )
      }

      self.onResumeRequest = { envoyHeaders, data, envoyTrailers, endStream, streamIntel in
        let result = asyncRequestFilter.onResumeRequest(
          headers: envoyHeaders.map(RequestHeaders.init),
          data: data,
          trailers: envoyTrailers.map(RequestTrailers.init),
          endStream: endStream,
          streamIntel: StreamIntel(streamIntel))
        switch result {
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterResumeStatusResumeIteration,
            headers?.caseSensitiveHeaders() as Any,
            data as Any,
            trailers?.caseSensitiveHeaders() as Any,
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

      self.onResumeResponse = { envoyHeaders, data, envoyTrailers, endStream, streamIntel in
        let result = asyncResponseFilter.onResumeResponse(
          headers: envoyHeaders.map(ResponseHeaders.init),
          data: data,
          trailers: envoyTrailers.map(ResponseTrailers.init),
          endStream: endStream,
          streamIntel: StreamIntel(streamIntel))
        switch result {
        case .resumeIteration(let headers, let data, let trailers):
          return [
            kEnvoyFilterResumeStatusResumeIteration,
            headers?.caseSensitiveHeaders() as Any,
            data as Any,
            trailers?.caseSensitiveHeaders() as Any,
          ]
        }
      }
    }
  }
}
