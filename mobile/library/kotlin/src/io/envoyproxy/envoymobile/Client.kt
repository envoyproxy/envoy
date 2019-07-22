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
}
