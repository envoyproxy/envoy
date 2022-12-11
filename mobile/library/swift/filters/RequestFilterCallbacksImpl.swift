@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy implementation of RequestFilterCallbacks
final class RequestFilterCallbacksImpl: NSObject {
  private let callbacks: EnvoyHTTPFilterCallbacks

  init(callbacks: EnvoyHTTPFilterCallbacks) {
    self.callbacks = callbacks
    super.init()
  }
}

extension RequestFilterCallbacksImpl: RequestFilterCallbacks {
  func resumeRequest() {
    self.callbacks.resumeIteration()
  }

  func resetIdleTimer() {
    self.callbacks.resetIdleTimer()
  }
}
