package io.envoyproxy.envoymobile

import java.nio.ByteBuffer

interface Client {
  /**
   * For starting a stream.
   *
   * @param request the request for opening a stream.
   * @param responseHandler the callback for receiving stream events.
   * @return the emitter for streaming data outward.
   */
  fun send(request: Request, responseHandler: ResponseHandler): StreamEmitter


  /**
   * Convenience function for sending a unary request.
   *
   * @param request  The request to send.
   * @param data Serialized data to send as the body of the request.
   * @param trailers Trailers to send with the request.
   * @param responseHandler the callback for receiving stream events.
   * @return CancelableStream, a cancelable request.
   */
  fun send(request: Request, data: ByteBuffer?, trailers: Map<String, List<String>>,
                responseHandler: ResponseHandler): CancelableStream

  /**
   * Convenience function for sending a unary request.
   *
   * @param request The request to send.
   * @param body Serialized data to send as the body of the request.
   * @param responseHandler the callback for receiving stream events.
   * @return CancelableStream, a cancelable request.
   */
  fun send(request: Request, body: ByteBuffer?,
                responseHandler: ResponseHandler): CancelableStream
}
