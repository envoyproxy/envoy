@_implementationOnly import EnvoyEngine
import Foundation

/// Mock implementation of `Stream` that also provides an interface for sending
/// mocked responses through to the stream's callbacks. Created via `MockStreamPrototype`.
@objcMembers
public final class MockStream: Stream {
  private let mockStream: MockEnvoyHTTPStream

  /// Closure that will be called when request headers are sent.
  public var onRequestHeaders: ((_ headers: RequestHeaders, _ endStream: Bool) -> Void)?
  /// Closure that will be called when request data is sent.
  public var onRequestData: ((_ data: Data, _ endStream: Bool) -> Void)?
  /// Closure that will be called when request trailers are sent.
  public var onRequestTrailers: ((_ trailers: RequestTrailers) -> Void)?
  /// Closure that will be called when the stream is canceled by the client.
  public var onCancel: (() -> Void)?

  init(mockStream: MockEnvoyHTTPStream) {
    self.mockStream = mockStream
    super.init(underlyingStream: mockStream)
  }

  // MARK: - Overrides

  public override func sendHeaders(_ headers: RequestHeaders, endStream: Bool) -> Stream {
    self.onRequestHeaders?(headers, endStream)
    return self
  }

  public override func sendData(_ data: Data) -> Stream {
    self.onRequestData?(data, false)
    return self
  }

  public override func close(data: Data) {
    self.onRequestData?(data, true)
  }

  public override func close(trailers: RequestTrailers) {
    self.onRequestTrailers?(trailers)
  }

  public override func cancel() {
    self.onCancel?()
  }

  // MARK: - Mocking responses

  /// Simulate response headers coming back over the stream.
  ///
  /// - parameter headers:   Response headers to receive.
  /// - parameter endStream: Whether this is a headers-only response.
  public func receiveHeaders(_ headers: ResponseHeaders, endStream: Bool) {
    self.mockStream.callbacks.onHeaders(headers.headers, endStream, EnvoyStreamIntel())
  }

  /// Simulate response data coming back over the stream.
  ///
  /// - parameter data:      Response data to receive.
  /// - parameter endStream: Whether this is the last data frame.
  public func receiveData(_ data: Data, endStream: Bool) {
    self.mockStream.callbacks.onData(data, endStream, EnvoyStreamIntel())
  }

  /// Simulate trailers coming back over the stream.
  ///
  /// - parameter trailers: Response trailers to receive.
  public func receiveTrailers(_ trailers: ResponseTrailers) {
    self.mockStream.callbacks.onTrailers(trailers.headers, EnvoyStreamIntel())
  }

  /// Simulate the stream receiving a cancellation signal from Envoy.
  public func receiveCancel() {
    self.mockStream.callbacks.onCancel(EnvoyStreamIntel())
  }

  /// Simulate Envoy returning an error.
  ///
  /// - parameter error: The error to receive.
  public func receiveError(_ error: EnvoyError) {
    self.mockStream.callbacks.onError(error.errorCode, error.message,
                                      Int32(error.attemptCount ?? 0),
                                      EnvoyStreamIntel())
  }
}
