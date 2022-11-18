package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel
import java.nio.ByteBuffer

/**
 * Mock implementation of `Stream` that also provides an interface for sending
 * mocked responses through to the stream's callbacks. Created via `MockStreamPrototype`.
 */
class MockStream internal constructor(underlyingStream: MockEnvoyHTTPStream) : Stream(underlyingStream, useByteBufferPosition = false) {
  private val mockStream: MockEnvoyHTTPStream = underlyingStream

  private val mockStreamIntel = object : EnvoyStreamIntel {
    override fun getStreamId(): Long { return 0 }
    override fun getConnectionId(): Long { return 0 }
    override fun getAttemptCount(): Long { return 0 }
    override fun getConsumedBytesFromResponse(): Long { return 0 }
  }

  private val mockFinalStreamIntel = object : EnvoyFinalStreamIntel {
    override fun getStreamStartMs(): Long { return 0 }
    override fun getDnsStartMs(): Long { return 0 }
    override fun getDnsEndMs(): Long { return 0 }
    override fun getConnectStartMs(): Long { return 0 }
    override fun getConnectEndMs(): Long { return 0 }
    override fun getSslStartMs(): Long { return 0 }
    override fun getSslEndMs(): Long { return 0 }
    override fun getSendingStartMs(): Long { return 0 }
    override fun getSendingEndMs(): Long { return 0 }
    override fun getResponseStartMs(): Long { return 0 }
    override fun getStreamEndMs(): Long { return 0 }
    override fun getSocketReused(): Boolean { return false }
    override fun getSentByteCount(): Long { return 0 }
    override fun getReceivedByteCount(): Long { return 0 }
    override fun getResponseFlags(): Long { return 0 }
    override fun getUpstreamProtocol(): Long { return 0 }
  }
  /**
   * Closure that will be called when request headers are sent.
   */
  var onRequestHeaders: ((headers: RequestHeaders, endStream: Boolean) -> Unit)? = null
  /**
   * Closure that will be called when request data is sent.
   */
  var onRequestData: ((data: ByteBuffer, endStream: Boolean) -> Unit)? = null
  /**
   * Closure that will be called when request trailers are sent.
   */
  var onRequestTrailers: ((trailers: RequestTrailers) -> Unit)? = null
  /**
   * Closure that will be called when the stream is canceled by the client.
   */
  var onCancel: (() -> Unit)? = null

  override fun sendHeaders(headers: RequestHeaders, endStream: Boolean): Stream {
    onRequestHeaders?.invoke(headers, endStream)
    return this
  }

  override fun sendData(data: ByteBuffer): Stream {
    onRequestData?.invoke(data, false)
    return this
  }

  override fun close(data: ByteBuffer) {
    onRequestData?.invoke(data, true)
  }

  override fun close(trailers: RequestTrailers) {
    onRequestTrailers?.invoke(trailers)
  }

  override fun cancel() {
    onCancel?.invoke()
  }

  /**
   * Simulate response headers coming back over the stream.
   *
   * @param headers Response headers to receive.
   * @param endStream Whether this is a headers-only response.
   */
  fun receiveHeaders(headers: ResponseHeaders, endStream: Boolean) {
    mockStream.callbacks.onHeaders(headers.caseSensitiveHeaders(), endStream, mockStreamIntel)
  }

  /**
   * Simulate response data coming back over the stream.
   *
   * @param data Response data to receive.
   * @param endStream Whether this is the last data frame.
   */
  fun receiveData(data: ByteBuffer, endStream: Boolean) {
    mockStream.callbacks.onData(data, endStream, mockStreamIntel)
  }

  /**
   * Simulate trailers coming back over the stream.
   *
   * @param trailers Response trailers to receive.
   */
  fun receiveTrailers(trailers: ResponseTrailers) {
    mockStream.callbacks.onTrailers(trailers.caseSensitiveHeaders(), mockStreamIntel)
  }

  /**
   * Simulate the stream receiving a cancellation signal from Envoy.
   */
  fun receiveCancel() {
    mockStream.callbacks.onCancel(mockStreamIntel, mockFinalStreamIntel)
  }

  /**
   * Simulate Envoy returning an error.
   *
   * @param error The error to receive.
   */
  fun receiveError(error: EnvoyError) {
    mockStream.callbacks.onError(error.errorCode, error.message, error.attemptCount ?: 0, mockStreamIntel, mockFinalStreamIntel)
  }
}
