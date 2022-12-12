import Foundation

/// Headers representing an outbound gRPC request.
@objcMembers
public final class GRPCRequestHeaders: RequestHeaders {
  /// Convert the headers back to a builder for mutation.
  ///
  /// - returns: The new builder.
  public func toGRPCRequestHeadersBuilder() -> GRPCRequestHeadersBuilder {
    return GRPCRequestHeadersBuilder(container: self.container)
  }
}
