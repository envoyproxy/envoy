@_implementationOnly import EnvoyCxxSwiftInterop
@_implementationOnly import EnvoyEngine

// swiftlint:disable force_unwrapping

final class SwiftEnvoyHTTPStreamImpl: EnvoyHTTPStream {
  private let engineHandle: envoy_engine_t
  private let streamHandle: envoy_stream_t
  private var streamAndCallbacksPointer: UnsafeMutableRawPointer?

  init(
    handle: Int, engine engineHandle: Int, callbacks: EnvoyHTTPCallbacks, explicitFlowControl: Bool
  ) {
    self.streamHandle = handle
    self.engineHandle = engineHandle
    let streamAndCallbacks = StreamAndCallbacks(callbacks: callbacks, stream: self)
    let context = PointerBox(value: streamAndCallbacks).retainedMutablePointer()
    self.streamAndCallbacksPointer = context
    var cppCallbacks = envoy_http_callbacks()
    cppCallbacks.on_headers = { headers, end_stream, stream_intel, context in
      let callbacks = PointerBox<StreamAndCallbacks>.unretained(from: context!).callbacks
      callbacks.dispatchQueue.async {
        callbacks.onHeaders?(toSwiftHeaders(headers), end_stream, stream_intel)
      }
      return nil
    }

    cppCallbacks.on_data = { data, end_stream, stream_intel, context in
      let callbacks = PointerBox<StreamAndCallbacks>.unretained(from: context!).callbacks
      callbacks.dispatchQueue.async {
        callbacks.onData?(Data(data), end_stream, stream_intel)
      }
      return nil
    }

    cppCallbacks.on_trailers = { trailers, stream_intel, context in
      let callbacks = PointerBox<StreamAndCallbacks>.unretained(from: context!).callbacks
      callbacks.dispatchQueue.async {
        callbacks.onTrailers?(toSwiftHeaders(trailers), stream_intel)
      }
      return nil
    }

    cppCallbacks.on_error = { error, stream_intel, final_stream_intel, context in
      let callbacks = PointerBox<StreamAndCallbacks>.unretained(from: context!).callbacks
      callbacks.dispatchQueue.async {
        let message = String.fromEnvoyData(error.message)!
        callbacks.onError?(
          UInt64(error.error_code.rawValue),
          message,
          error.attempt_count,
          stream_intel,
          final_stream_intel
        )
        release_envoy_error(error)
      }
      return nil
    }

    cppCallbacks.on_complete = { stream_intel, final_stream_intel, context in
      let callbacks = PointerBox<StreamAndCallbacks>.unretained(from: context!).callbacks
      callbacks.dispatchQueue.async {
        callbacks.onComplete?(stream_intel, final_stream_intel)
      }
      return nil
    }

    cppCallbacks.on_cancel = { stream_intel, final_stream_intel, context in
      let callbacks = PointerBox<StreamAndCallbacks>.unretained(from: context!).callbacks
      callbacks.dispatchQueue.async {
        callbacks.onCancel?(stream_intel, final_stream_intel)
      }
      return nil
    }

    cppCallbacks.on_send_window_available = { stream_intel, context in
      let callbacks = PointerBox<StreamAndCallbacks>.unretained(from: context!).callbacks
      callbacks.dispatchQueue.async {
        callbacks.onSendWindowAvailable?(stream_intel)
      }
      return nil
    }

    cppCallbacks.context = context

    start_stream(engineHandle, handle, cppCallbacks, explicitFlowControl)
  }

  func cancel() -> Int32 {
    Int32(reset_stream(self.engineHandle, self.streamHandle).rawValue)
  }

  func sendHeaders(_ headers: [String: [String]], close: Bool) {
    send_headers(self.engineHandle, self.streamHandle, toEnvoyHeaders(headers), close)
  }

  func readData(_ byteCount: Int) {
    read_data(self.engineHandle, self.streamHandle, byteCount)
  }

  func send(_ data: Data, close: Bool) {
    send_data(self.engineHandle, self.streamHandle, data.toEnvoyData(), close)
  }

  func sendTrailers(_ trailers: [String: [String]]) {
    send_trailers(self.engineHandle, self.streamHandle, toEnvoyHeaders(trailers))
  }

  func cleanUp() {
    guard let pointer = self.streamAndCallbacksPointer else {
      return
    }

    PointerBox<StreamAndCallbacks>.unmanaged(from: pointer).release()
  }
}

private final class StreamAndCallbacks {
  let callbacks: EnvoyHTTPCallbacks
  let stream: SwiftEnvoyHTTPStreamImpl
  init(callbacks: EnvoyHTTPCallbacks, stream: SwiftEnvoyHTTPStreamImpl) {
    self.callbacks = callbacks
    self.stream = stream
  }
}
