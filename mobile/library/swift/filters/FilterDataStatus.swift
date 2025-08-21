import Foundation

/// Status returned by filters when transmitting or receiving data.
@frozen
public enum FilterDataStatus<T: Headers>: Equatable {
  /// Continue filter chain iteration. If headers have not yet been sent to the next filter, they
  /// will be sent first via `onRequestHeaders()`/`onResponseHeaders()`.
  ///
  /// - param data: The (potentially-modified) data to be forwarded along the filter chain.
  case `continue`(data: Data)

  /// Do not iterate to any of the remaining filters in the chain, and buffer body data for later
  /// dispatching. The data passed to this invocation will be buffered internally.
  ///
  /// `onData` will continue to be called with any new chunks of data appended to all data that has
  /// been buffered so far.
  ///
  /// Returning `resumeIteration from another filter invocation or calling
  /// `resumeRequest()`/`resumeResponse()` MUST be called when continued filter iteration is
  /// desired.
  ///
  /// This should be called by filters which must parse a larger block of the incoming data before
  /// continuing processing.
  case stopIterationAndBuffer

  /// Do not iterate to any of the remaining filters in the chain, and do not internally buffer
  /// data.
  ///
  /// `onData` will continue to be called with new chunks of data.
  ///
  /// Returning `resumeIteration` from another filter invocation or calling
  /// `resumeRequest()`/`resumeResponse()` MUST be called when continued filter iteration is
  /// desired.
  ///
  /// This may be called by filters which must parse a larger block of the incoming data before
  /// continuing processing, and will handle their own buffering.
  case stopIterationNoBuffer

  /// Resume previously-stopped iteration, possibly forwarding headers if iteration was stopped
  /// during an on*Headers invocation.
  ///
  /// It is an error to return `resumeIteration` if iteration is not currently stopped, and it is
  /// an error to include headers if headers have already been forwarded to the next filter
  /// (i.e. iteration was stopped during an on*Data invocation instead of on*Headers).
  ///
  /// - param headers: Headers to be forwarded (if needed).
  /// - param data: Data to be forwarded.
  case resumeIteration(headers: T? = nil, data: Data)
}
