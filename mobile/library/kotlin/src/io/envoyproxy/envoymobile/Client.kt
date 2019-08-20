package io.envoyproxy.envoymobile

interface Client {
  /**
   * For starting a stream.
   *
   * @param request the request for opening a stream.
   * @param responseHandler the callback for receiving stream events.
   * @return the emitter for streaming data outward.
   */
  fun startStream(request: Request, responseHandler: ResponseHandler): StreamEmitter


  /**
   * Convenience function for sending a unary request.
   *
   * @param request  The request to send.
   * @param body Serialized data to send as the body of the request.
   * @param trailers Trailers to send with the request.
   * @param responseHandler the callback for receiving stream events.
   */
  fun sendUnary(request: Request, body: ByteArray?, trailers: Map<String, List<String>>, responseHandler: ResponseHandler)

  /**
   * Convenience function for sending a unary request.
   *
   * @param request The request to send.
   * @param body Serialized data to send as the body of the request.
   * @param responseHandler the callback for receiving stream events.
   */
  fun sendUnary(request: Request, body: ByteArray?, responseHandler: ResponseHandler)
}
