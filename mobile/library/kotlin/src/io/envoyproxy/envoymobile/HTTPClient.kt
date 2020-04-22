package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

interface HTTPClient {
  /**
   * Start a new stream.
   *
   * @param request The request headers for opening a stream.
   * @param responseHandler Handler for receiving stream events.
   * @return Emitter for sending streaming data outward.
   */
  fun start(request: Request, responseHandler: ResponseHandler): StreamEmitter

  /**
   * Send a unary (non-streamed) request.
   * Close with headers-only: Pass a nil body and nil trailers.
   * Close with data:         Pass a body and nil trailers.
   * Close with trailers:     Pass non-nil trailers.
   *
   * @param request  The request headers to send.
   * @param body Data to send as the body.
   * @param trailers Trailers to send with the request.
   * @param responseHandler Handler for receiving response events.
   * @return A cancelable request.
   */
  fun send(
    request: Request, body: ByteBuffer?, trailers: Map<String, List<String>>?,
    responseHandler: ResponseHandler): CancelableStream
}
