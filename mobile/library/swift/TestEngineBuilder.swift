import Foundation

/// Builder that can be used to construct instances of the Envoy engine
/// which have the added functionality of returning "direct" responses to
/// requests over a local connection. This can be particularly useful for mocking/testing.
public final class TestEngineBuilder: EngineBuilder {
  /// Adds a direct response configuration which will be used when starting the engine.
  /// Doing so will cause Envoy to clear its route cache for each stream in order to allow
  /// filters to mutate headers (which can subsequently affect routing).
  ///
  /// - parameter response: The response configuration to add.
  ///
  /// - returns: This builder.
  @discardableResult
  public func addDirectResponse(_ response: DirectResponse) -> Self {
    self.addDirectResponseInternal(response)
    return self
  }
}
