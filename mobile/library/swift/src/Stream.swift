@_implementationOnly import EnvoyEngine
import Foundation

/// A type representing a stream that is actively transferring data.
///
/// Constructed using `StreamPrototype`, and used to write to the network.
@objcMembers
public class Stream: NSObject {
  private let underlyingStream: EnvoyHTTPStream

  /// Initialize a new instance of the stream.
  ///
  /// - parameter underlyingStream: Underlying stream that can be used to send request data.
  init(underlyingStream: EnvoyHTTPStream) {
    self.underlyingStream = underlyingStream
    super.init()
  }

  // MARK: - Public

  /// Send headers over the stream.
  ///
  /// - parameter headers:   Headers to send over the stream.
  /// - parameter endStream: Whether this is a headers-only request.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func sendHeaders(_ headers: RequestHeaders, endStream: Bool) -> Stream {
    self.underlyingStream.sendHeaders(headers.headers, close: endStream)
    return self
  }

  /// Send data over the stream.
  ///
  /// - parameter data: Data to send over the stream.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func sendData(_ data: Data) -> Stream {
    self.underlyingStream.send(data, close: false)
    return self
  }

  /// Close the stream with trailers.
  ///
  /// - parameter trailers: Trailers with which to close the stream.
  public func close(trailers: RequestTrailers) {
    self.underlyingStream.sendTrailers(trailers.headers)
  }

  /// Close the stream with a data frame.
  ///
  /// - parameter data: Data with which to close the stream.
  public func close(data: Data) {
    self.underlyingStream.send(data, close: true)
  }

  /// Cancel the stream.
  public func cancel() {
    _ = self.underlyingStream.cancel()
  }
}
