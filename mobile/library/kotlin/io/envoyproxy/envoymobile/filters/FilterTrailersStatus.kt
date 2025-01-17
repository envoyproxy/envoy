package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

/*
 * Status to be returned by filters when transmitting or receiving trailers.
 */
sealed class FilterTrailersStatus<T : Headers, U : Trailers>(val status: Int) {
  /**
   * Continue filter chain iteration, passing the provided trailers through.
   *
   * @param trailers: The (potentially-modified) trailers to be forwarded along the filter chain.
   */
  class Continue<T : Headers, U : Trailers>(val trailers: U) : FilterTrailersStatus<T, U>(0)

  /**
   * Do not iterate to any of the remaining filters in the chain with trailers.
   *
   * Because trailers are by definition the last HTTP entity of a request or response, only
   * asynchronous filters support resumption after returning `StopIteration` from on*Trailers.
   * Calling `resumeRequest()`/`resumeResponse()` MUST occur if continued filter iteration is
   * desired.
   */
  class StopIteration<T : Headers, U : Trailers> : FilterTrailersStatus<T, U>(1)

  /**
   * Resume previously-stopped iteration, possibly forwarding headers and data if iteration was
   * stopped during an on*Headers or on*Data invocation.
   *
   * It is an error to return `ResumeIteration` if iteration is not currently stopped, and it is an
   * error to include headers if headers have already been forwarded to the next filter (i.e.
   * iteration was stopped during an on*Data invocation instead of on*Headers).
   *
   * @param headers: Headers to be forwarded (if needed).
   * @param data: Data to be forwarded (if needed).
   * @param trailers: Trailers to be forwarded.
   */
  class ResumeIteration<T : Headers, U : Trailers>(
    val headers: T?,
    val data: ByteBuffer?,
    val trailers: U
  ) : FilterTrailersStatus<T, U>(-1)
}
