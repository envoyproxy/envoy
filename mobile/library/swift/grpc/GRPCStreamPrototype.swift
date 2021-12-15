import Foundation

/// A type representing a gRPC stream that has not yet been started.
///
/// Constructed via `GRPCClient`, and used to assign response callbacks
/// prior to starting a `GRPCStream` by calling `start()`.
@objcMembers
public final class GRPCStreamPrototype: NSObject {
  private let underlyingStream: StreamPrototype

  /// Initialize a new instance of the inactive gRPC stream.
  ///
  /// - parameter underlyingStream: The underlying stream to use.
  required init(underlyingStream: StreamPrototype) {
    self.underlyingStream = underlyingStream
    super.init()
  }

  // MARK: - Public

  /// Start a new gRPC stream.
  ///
  /// - parameter queue: Queue on which to receive callback events.
  ///
  /// - returns: The new gRPC stream.
  public func start(queue: DispatchQueue = .main) -> GRPCStream {
    let stream = self.underlyingStream.start(queue: queue)
    return GRPCStream(underlyingStream: stream)
  }

  /// Specify a callback for when response headers are received by the stream.
  ///
  /// - parameter closure: Closure which will receive the headers
  ///                      and flag indicating if the stream is headers-only.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnResponseHeaders(
    closure: @escaping (_ headers: ResponseHeaders, _ endStream: Bool,
                        _ streamIntel: StreamIntel) -> Void
  ) -> GRPCStreamPrototype {
    self.underlyingStream.setOnResponseHeaders(closure: closure)
    return self
  }

  /// Specify a callback for when a new message has been received by the stream.
  ///
  /// - parameter closure: Closure which will receive messages on the stream.
  ///
  /// - returns: This handler, which may be used for chaining syntax.
  @discardableResult
  public func setOnResponseMessage(
    _ closure: @escaping (_ message: Data, _ streamIntel: StreamIntel) -> Void
  ) -> GRPCStreamPrototype {
    var buffer = Data()
    var state = GRPCMessageProcessor.State.expectingCompressionFlag
    self.underlyingStream.setOnResponseData { chunk, _, streamIntel in
      // Appending might result in extra copying that can be optimized in the future.
      buffer.append(chunk)
      // gRPC always sends trailers, so the stream will not complete here.
      GRPCMessageProcessor.processBuffer(&buffer, state: &state, streamIntel: streamIntel,
                                         onMessage: closure)
    }

    return self
  }

  /// Specify a callback for when trailers are received by the stream.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will receive the trailers.
  ///
  /// - returns: This handler, which may be used for chaining syntax.
  @discardableResult
  public func setOnResponseTrailers(_ closure:
    @escaping (_ trailers: ResponseTrailers, _ streamIntel: StreamIntel) -> Void
  ) -> GRPCStreamPrototype {
    self.underlyingStream.setOnResponseTrailers(closure: closure)
    return self
  }

  /// Specify a callback for when an internal Envoy exception occurs with the stream.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will be called when an error occurs.
  ///
  /// - returns: This handler, which may be used for chaining syntax.
  @discardableResult
  public func setOnError(
    _ closure: @escaping (_ error: EnvoyError, _ streamIntel: StreamIntel) -> Void
  ) -> GRPCStreamPrototype {
    self.underlyingStream.setOnError(closure: closure)
    return self
  }

  /// Specify a callback for when the stream is canceled.
  /// If the closure is called, the stream is complete.
  ///
  /// - parameter closure: Closure which will be called when the stream is canceled.
  ///
  /// - returns: This stream, for chaining syntax.
  @discardableResult
  public func setOnCancel(
    closure: @escaping (_ streamInte: StreamIntel) -> Void
  ) -> GRPCStreamPrototype {
    self.underlyingStream.setOnCancel(closure: closure)
    return self
  }
}

private enum GRPCMessageProcessor {
  /// Represents the state of a response stream's body data.
  enum State {
    /// Awaiting a gRPC compression flag.
    case expectingCompressionFlag
    /// Awaiting the length specification of the next message.
    case expectingMessageLength
    /// Awaiting a message with the specified length.
    case expectingMessage(messageLength: UInt32)
  }

  /// Recursively processes a buffer of data, buffering it into messages based on state.
  /// When a message has been fully buffered, `onMessage` will be called with the message.
  ///
  /// - parameter buffer:    The buffer of data from which to determine state and messages.
  /// - parameter state:     The current state of the buffering.
  /// - parameter onMessage: Closure to call when a new message is available.
  static func processBuffer(_ buffer: inout Data, state: inout State, streamIntel: StreamIntel,
                            onMessage: (_ message: Data, _ streamIntel: StreamIntel) -> Void)
  {
    switch state {
    case .expectingCompressionFlag:
      guard let compressionFlag: UInt8 = buffer.integer(atIndex: 0) else {
        return
      }

      guard compressionFlag == 0 else {
        // TODO: Support gRPC compression https://github.com/envoyproxy/envoy-mobile/issues/501
        buffer.removeAll()
        state = .expectingCompressionFlag
        return
      }

      state = .expectingMessageLength

    case .expectingMessageLength:
      guard let messageLength: UInt32 = buffer.integer(atIndex: 1) else {
        return
      }

      state = .expectingMessage(messageLength: CFSwapInt32BigToHost(messageLength))

    case .expectingMessage(let messageLength):
      let prefixedLength = kGRPCPrefixLength + Int(messageLength)
      if buffer.count < prefixedLength {
        return
      }

      if messageLength > 0 {
        onMessage(buffer.subdata(in: kGRPCPrefixLength..<prefixedLength), streamIntel)
      } else {
        onMessage(Data(), streamIntel)
      }

      buffer.removeSubrange(0..<prefixedLength)
      state = .expectingCompressionFlag
    }

    self.processBuffer(&buffer, state: &state, streamIntel: streamIntel, onMessage: onMessage)
  }
}
