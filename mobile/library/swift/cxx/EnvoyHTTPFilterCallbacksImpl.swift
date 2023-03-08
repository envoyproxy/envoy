@_implementationOnly import EnvoyCxxSwiftInterop
@_implementationOnly import EnvoyEngine

final class EnvoyHTTPFilterCallbacksImpl: EnvoyHTTPFilterCallbacks {
  let callbacks: envoy_http_filter_callbacks
  init(callbacks: envoy_http_filter_callbacks) {
    self.callbacks = callbacks
  }

  func resumeIteration() {
    self.callbacks.resume_iteration(self.callbacks.callback_context)
  }

  func resetIdleTimer() {
    self.callbacks.reset_idle(self.callbacks.callback_context)
  }

  deinit {
    self.callbacks.release_callbacks(self.callbacks.callback_context)
  }
}
