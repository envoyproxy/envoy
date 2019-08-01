package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyData;
import io.envoyproxy.envoymobile.engine.types.EnvoyHeaders;
import io.envoyproxy.envoymobile.engine.types.EnvoyObserver;
import io.envoyproxy.envoymobile.engine.types.EnvoyStatus;
import io.envoyproxy.envoymobile.engine.types.EnvoyStream;

public interface Engine {
  /**
   * Open an underlying HTTP stream.
   *
   * @param observer, the observer that will run the stream callbacks.
   * @return EnvoyStream, with a stream handle and a success status, or a failure status.
   */
  EnvoyStream startStream(EnvoyObserver observer);

  /**
   * Send headers over an open HTTP stream. This method can be invoked once and needs to be called
   * before send_data.
   *
   * @param stream,    the stream to send headers over.
   * @param headers,   the headers to send.
   * @param endStream, supplies whether this is headers only.
   * @return EnvoyStatus, the resulting status of the operation.
   */
  EnvoyStatus sendHeaders(EnvoyStream stream, EnvoyHeaders headers, boolean endStream);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple times.
   *
   * @param stream,    the stream to send data over.
   * @param data,      the data to send.
   * @param endStream, supplies whether this is the last data in the stream.
   * @return EnvoyStatus, the resulting status of the operation.
   */
  EnvoyStatus sendData(EnvoyStream stream, EnvoyData data, boolean endStream);

  /**
   * Send metadata over an HTTP stream. This method can be invoked multiple times.
   *
   * @param stream,    the stream to send metadata over.
   * @param metadata,  the metadata to send.
   * @param endStream, supplies whether this is the last data in the stream.
   * @return EnvoyStatus, the resulting status of the operation.
   */
  EnvoyStatus sendMetadata(EnvoyStream stream, EnvoyHeaders metadata, boolean endStream);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly ends the stream.
   *
   * @param stream,   the stream to send trailers over.
   * @param trailers, the trailers to send.
   * @return EnvoyStatus, the resulting status of the operation.
   */
  EnvoyStatus sendTrailers(EnvoyStream stream, EnvoyHeaders trailers);

  /**
   * Half-close an HTTP stream. The stream will be observable and may return further data
   * via the observer callbacks. However, nothing further may be sent.
   *
   * @param stream, the stream to close.
   * @return EnvoyStatus, the resulting status of the operation.
   */
  EnvoyStatus locallyCloseStream(EnvoyStream stream);

  /**
   * Detach all observers from a stream and send an interrupt upstream if supported by transport.
   *
   * @param stream, the stream to evict.
   * @return EnvoyStatus, the resulting status of the operation.
   */
  EnvoyStatus resetStream(EnvoyStream stream);

  /**
   * External entrypoint for library.
   *
   * @param config,   the configuration blob to run envoy with.
   * @param logLevel, the logging level to run envoy with.
   * @return EnvoyStatus, the resulting status of the operation.
   */
  EnvoyStatus runEngine(String config, String logLevel);
}
