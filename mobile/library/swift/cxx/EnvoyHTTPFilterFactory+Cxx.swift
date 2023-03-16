@_implementationOnly import EnvoyCxxSwiftInterop
@_implementationOnly import EnvoyEngine
import Foundation

// swiftlint:disable force_cast force_unwrapping

extension EnvoyHTTPFilterFactory {
  // swiftlint:disable:next cyclomatic_complexity
  func register() {
    let filter = UnsafeMutablePointer<envoy_http_filter>.allocate(capacity: 1)
    filter.pointee.init_filter = { context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let c_filter = context!.load(as: envoy_http_filter.self)
        let filterFactory = PointerBox<EnvoyHTTPFilterFactory>
          .unretained(from: c_filter.static_context)
        let filter = filterFactory.create()
        return UnsafeRawPointer(Unmanaged.passRetained(filter).toOpaque())
      }
    }
    filter.pointee.on_request_headers = { headers, end_stream, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let onRequestHeaders = filter.onRequestHeaders else {
          return envoy_filter_headers_status(
            status: kEnvoyFilterHeadersStatusContinue,
            headers: headers
          )
        }
        // TODO(goaway): optimize unmodified case
        let platformHeaders = toSwiftHeaders(headers)
        // TODO(goaway): consider better solution for compound return
        let result = onRequestHeaders(platformHeaders, end_stream, stream_intel)
        return envoy_filter_headers_status(
          status: envoy_filter_headers_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          headers: toEnvoyHeaders(result[1] as? [String: [String]])
        )
      }
    }

    filter.pointee.on_request_data = { data, end_stream, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard
          let onRequestData = filter.onRequestData
        else {
          return envoy_filter_data_status(
            status: kEnvoyFilterHeadersStatusContinue,
            data: data,
            pending_headers: nil
          )
        }

        let platformData = Data(data)
        let result = onRequestData(platformData, end_stream, stream_intel)
        // Result is typically a pair of status and entity, but uniquely in the case of
        // ResumeIteration it will (optionally) contain additional pending elements.
        let pending_headers = toEnvoyHeadersPtr(
          result.count == 3 ? (result[2] as? [String: [String]]) : nil
        )
        return envoy_filter_data_status(
          status: envoy_filter_data_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          data: (result[1] as? Data).toEnvoyData(),
          pending_headers: pending_headers
        )
      }
    }

    filter.pointee.on_request_trailers = { trailers, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard
          let onRequestTrailers = filter.onRequestTrailers
        else {
          return envoy_filter_trailers_status(
            status: kEnvoyFilterTrailersStatusContinue,
            trailers: trailers,
            pending_headers: nil,
            pending_data: nil
          )
        }

        let platformTrailers = toSwiftHeaders(trailers)
        let result = onRequestTrailers(platformTrailers, stream_intel)
        var pending_headers: UnsafeMutablePointer<envoy_headers>?
        var pending_data: UnsafeMutablePointer<envoy_data>?
        // Result is typically a pair of status and entity, but uniquely in the case of
        // ResumeIteration it will (optionally) contain additional pending elements.
        if result.count == 4 {
          pending_headers = toEnvoyHeadersPtr(result[2] as? [String: [String]])
          pending_data = toEnvoyDataPtr(result[3] as? Data)
        }

        return envoy_filter_trailers_status(
          status: envoy_filter_trailers_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          trailers: toEnvoyHeaders(result[1] as? [String: [String]]),
          pending_headers: pending_headers,
          pending_data: pending_data
        )
      }
    }
    filter.pointee.on_response_headers = { headers, end_stream, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let onResponseHeaders = filter.onResponseHeaders else {
          return envoy_filter_headers_status(
            status: kEnvoyFilterHeadersStatusContinue,
            headers: headers
          )
        }
        // TODO(goaway): optimize unmodified case
        let platformHeaders = toSwiftHeaders(headers)
        // TODO(goaway): consider better solution for compound return
        let result = onResponseHeaders(platformHeaders, end_stream, stream_intel)
        return envoy_filter_headers_status(
          status: envoy_filter_headers_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          headers: toEnvoyHeaders(result[1] as? [String: [String]])
        )
      }
    }
    filter.pointee.on_response_data = { data, end_stream, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard
          let onResponseData = filter.onResponseData
        else {
          return envoy_filter_data_status(
            status: kEnvoyFilterHeadersStatusContinue,
            data: data,
            pending_headers: nil
          )
        }

        let platformData = Data(data)
        let result = onResponseData(platformData, end_stream, stream_intel)
        // Result is typically a pair of status and entity, but uniquely in the case of
        // ResumeIteration it will (optionally) contain additional pending elements.
        let pending_headers = toEnvoyHeadersPtr(
          result.count == 3 ? (result[2] as? [String: [String]]) : nil
        )
        return envoy_filter_data_status(
          status: envoy_filter_data_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          data: (result[1] as? Data).toEnvoyData(),
          pending_headers: pending_headers
        )
      }
    }
    filter.pointee.on_response_trailers = { trailers, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard
          let onResponseTrailers = filter.onResponseTrailers
        else {
          return envoy_filter_trailers_status(
            status: kEnvoyFilterTrailersStatusContinue,
            trailers: trailers,
            pending_headers: nil,
            pending_data: nil
          )
        }

        let platformTrailers = toSwiftHeaders(trailers)
        let result = onResponseTrailers(platformTrailers, stream_intel)
        var pending_headers: UnsafeMutablePointer<envoy_headers>?
        var pending_data: UnsafeMutablePointer<envoy_data>?
        // Result is typically a pair of status and entity, but uniquely in the case of
        // ResumeIteration it will (optionally) contain additional pending elements.
        if result.count == 4 {
          pending_headers = toEnvoyHeadersPtr(result[2] as? [String: [String]])
          pending_data = toEnvoyDataPtr(result[3] as? Data)
        }

        return envoy_filter_trailers_status(
          status: envoy_filter_trailers_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          trailers: toEnvoyHeaders(result[1] as? [String: [String]]),
          pending_headers: pending_headers,
          pending_data: pending_data
        )
      }
    }
    filter.pointee.set_request_callbacks = { callbacks, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let setRequestFilterCallbacks = filter.setRequestFilterCallbacks else {
          return
        }

        let requestFilterCallbacks = EnvoyHTTPFilterCallbacksImpl(callbacks: callbacks)
        setRequestFilterCallbacks(requestFilterCallbacks)
      }
    }
    filter.pointee.set_response_callbacks = { callbacks, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let setResponseFilterCallbacks = filter.setResponseFilterCallbacks else {
          return
        }

        let responseFilterCallbacks = EnvoyHTTPFilterCallbacksImpl(callbacks: callbacks)
        setResponseFilterCallbacks(responseFilterCallbacks)
      }
    }

    filter.pointee.on_resume_request =
    { headers, data, trailers, end_stream, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let onResumeRequest = filter.onResumeRequest else {
          return envoy_filter_resume_status(
            status: kEnvoyFilterHeadersStatusContinue,
            pending_headers: headers,
            pending_data: data,
            pending_trailers: trailers
          )
        }
        let pendingHeaders = (headers?.pointee).flatMap(toSwiftHeaders)
        let pendingData = (data?.pointee).flatMap(Data.init)
        let pendingTrailers = (trailers?.pointee).flatMap(toSwiftHeaders)
        let result = onResumeRequest(pendingHeaders, pendingData, pendingTrailers, end_stream,
                                     stream_intel)
        return envoy_filter_resume_status(
          status: envoy_filter_resume_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          pending_headers: toEnvoyHeadersPtr(result[1] as? [String: [String]]),
          pending_data: toEnvoyDataPtr(result[2] as? Data),
          pending_trailers: toEnvoyHeadersPtr(result[3] as? [String: [String]])
        )
      }
    }

    filter.pointee.on_resume_response =
    { headers, data, trailers, end_stream, stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let onResumeResponse = filter.onResumeResponse else {
          return envoy_filter_resume_status(
            status: kEnvoyFilterHeadersStatusContinue,
            pending_headers: headers,
            pending_data: data,
            pending_trailers: trailers
          )
        }
        let pendingHeaders = (headers?.pointee).flatMap(toSwiftHeaders)
        let pendingData = (data?.pointee).flatMap(Data.init)
        let pendingTrailers = (trailers?.pointee).flatMap(toSwiftHeaders)
        let result = onResumeResponse(pendingHeaders, pendingData, pendingTrailers, end_stream,
                                      stream_intel)
        return envoy_filter_resume_status(
          status: envoy_filter_resume_status_t((result[0] as? NSNumber)?.intValue ?? 0),
          pending_headers: toEnvoyHeadersPtr(result[1] as? [String: [String]]),
          pending_data: toEnvoyDataPtr(result[2] as? Data),
          pending_trailers: toEnvoyHeadersPtr(result[3] as? [String: [String]])
        )
      }
    }

    // TODO(goaway) HTTP filter on_complete not currently implemented.
    // api->on_complete = ios_http_filter_on_complete;
    filter.pointee.on_cancel = { stream_intel, final_stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let onCancel = filter.onCancel else {
          return
        }

        onCancel(stream_intel, final_stream_intel)
      }
    }

    filter.pointee.on_error = { error, stream_intel, final_stream_intel, context in
      // This closure runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
      // is necessary to act as a breaker for any Objective-C/Swift allocations that happen.
      autoreleasepool {
        defer {
          release_envoy_error(error)
        }

        let filter = PointerBox<EnvoyHTTPFilter>.unretained(from: context!)
        guard let onError = filter.onError else {
          return
        }

        let message = String.fromEnvoyData(error.message)!
        onError(
          UInt64(error.error_code.rawValue),
          message,
          error.attempt_count,
          stream_intel,
          final_stream_intel
        )
      }
    }

    filter.pointee.release_filter = { context in
      PointerBox<EnvoyHTTPFilter>.unmanaged(from: context!).release()
    }

    filter.pointee.static_context = PointerBox<EnvoyHTTPFilterFactory>(value: self)
      .retainedPointer()
    filter.pointee.instance_context = nil
    register_platform_api(self.filterName, filter)
  }
}
