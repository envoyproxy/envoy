package io.envoyproxy.envoymobile.engine.types;

public interface EnvoyObserver {
  /**
   * Called when all headers get received on the async HTTP stream.
   *
   * @param headers,   the headers received.
   * @param endStream, whether the response is headers-only.
   *                   execution.
   */
  void onHeaders(EnvoyHeaders headers, boolean endStream);

  /**
   * Called when a data frame gets received on the async HTTP stream.
   * This callback can be invoked multiple times if the data gets streamed.
   *
   * @param data,      the data received.
   * @param endStream, whether the data is the last data frame.
   *                   execution.
   */
  void onData(EnvoyData data, boolean endStream);

  /**
   * Called when all metadata get received on the async HTTP stream.
   * Note that end stream is implied when on_metadata is called.
   *
   * @param metadata, the metadata received.
   *                  execution.
   */
  void onMetadata(EnvoyHeaders metadata);

  /**
   * Called when all trailers get received on the async HTTP stream.
   * Note that end stream is implied when on_trailers is called.
   *
   * @param trailers, the trailers received.
   *                  execution.
   */
  void onTrailers(EnvoyHeaders trailers);

  /**
   * Called when the async HTTP stream has an error.
   *
   * @param error, the error received/caused by the async HTTP stream.
   *               execution.
   */
  void onError(EnvoyError error);
}
