package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

/*
 * Status to be returned by filters after resuming iteration asynchronously.
 */
sealed class FilterResumeStatus<T : Headers, U : Headers> {
  /**
   * Resume previously-stopped iteration, potentially forwarding headers, data, and/or trailers
   * that have not yet been passed along the filter chain.
   *
   * It is an error to return ResumeIteration if iteration is not currently stopped, and it is
   * an error to include headers if headers have already been forwarded to the next filter
   * (i.e. iteration was stopped during an on*Data invocation instead of on*Headers). It is also
   * an error to include data or trailers if `endStream` was previously set or if trailers have
   * already been forwarded.
   *
   * @param headers: Headers to be forwarded (if needed).
   * @param data: Data to be forwarded (if needed).
   * @param trailers: Trailers to be forwarded (if needed).
   */
  class ResumeIteration<T : Headers, U : Headers>(
    val headers: T?,
    val data: ByteBuffer?,
    val trailers: U?
  ) : FilterResumeStatus<T, U>()
}
