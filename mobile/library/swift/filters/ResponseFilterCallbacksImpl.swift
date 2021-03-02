@_implementationOnly import EnvoyEngine
import Foundation

/// Envoy implementation of ResponseFilterCallbacks
final class ResponseFilterCallbacksImpl: NSObject {
  private let callbacks: EnvoyHTTPFilterCallbacks

  init(callbacks: EnvoyHTTPFilterCallbacks) {
    self.callbacks = callbacks
    super.init()
  }
}

extension ResponseFilterCallbacksImpl: ResponseFilterCallbacks {
  func resumeResponse() {
    self.callbacks.resumeIteration()
  }
}
