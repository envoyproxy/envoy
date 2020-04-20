import Foundation

/// Default implementation of the `StreamEmitter` interface.
@objcMembers
final class EnvoyStreamEmitter {
  private let stream: EnvoyHTTPStream

  init(stream: EnvoyHTTPStream) {
    self.stream = stream
  }
}

extension EnvoyStreamEmitter: StreamEmitter {
  func sendData(_ data: Data) -> StreamEmitter {
    self.stream.send(data, close: false)
    return self
  }

  func sendMetadata(_ metadata: [String: [String]]) -> StreamEmitter {
    self.stream.sendMetadata(metadata)
    return self
  }

  func close(trailers: [String: [String]]) {
    self.stream.sendTrailers(trailers)
  }

  func close(data: Data) {
    self.stream.send(data, close: true)
  }

  func cancel() {
    _ = self.stream.cancel()
  }
}
