import Dispatch
@_implementationOnly import EnvoyEngine
import Foundation

/// A collection of platform-level callbacks that are specified by consumers
/// who wish to interact with streams.
///
/// `StreamCallbacks` are bridged through to `EnvoyHTTPCallbacks` to communicate with the engine.
final class StreamCallbacks {
  var onHeaders: ((_ headers: ResponseHeaders, _ endStream: Bool) -> Void)?
  var onData: ((_ body: Data, _ endStream: Bool) -> Void)?
  var onTrailers: ((_ trailers: ResponseTrailers) -> Void)?
  var onCancel: (() -> Void)?
  var onError: ((_ error: EnvoyError) -> Void)?
}

extension EnvoyHTTPCallbacks {
  /// Initializer propagating the platform callbacks into callbacks that the engine can use.
  ///
  /// - parameter callbacks: Platform callbacks to use.
  /// - parameter queue:     Queue on which to receive callback events.
  convenience init(callbacks: StreamCallbacks, queue: DispatchQueue) {
    self.init()
    self.dispatchQueue = queue
    self.onHeaders = { callbacks.onHeaders?(ResponseHeaders(headers: $0), $1) }
    self.onData = { callbacks.onData?($0, $1) }
    self.onTrailers = { callbacks.onTrailers?(ResponseTrailers(headers: $0)) }
    self.onCancel = { callbacks.onCancel?() }
    self.onError = { errorCode, message, attemptCount in
      // The initializer below will return nil if `attemptCount` is negative.
      // This is the desired behavior because the bridge layer uses -1 to signify absence.
      let error = EnvoyError(errorCode: errorCode, message: message,
                             attemptCount: UInt32(exactly: attemptCount), cause: nil)
      callbacks.onError?(error)
    }
  }
}
