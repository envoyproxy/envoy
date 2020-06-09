import Envoy
import EnvoyEngine
import Foundation

final class MockEnvoyHTTPStream: EnvoyHTTPStream {
  var onHeaders: (([String: [String]], Bool) -> Void)?
  var onData: ((Data, Bool) -> Void)?
  var onTrailers: (([String: [String]]) -> Void)?

  init(handle: Int, callbacks: EnvoyHTTPCallbacks) {}

  func sendHeaders(_ headers: [String: [String]], close: Bool) {
    self.onHeaders?(headers, close)
  }

  func send(_ data: Data, close: Bool) {
    self.onData?(data, close)
  }

  func sendTrailers(_ trailers: [String: [String]]) {
    self.onTrailers?(trailers)
  }

  func cancel() -> Int32 { return 0 }

  func cleanUp() {}
}
