package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyData;
import io.envoyproxy.envoymobile.engine.types.EnvoyHeaders;
import io.envoyproxy.envoymobile.engine.types.EnvoyObserver;

class JniLibrary {
  /**
   * Initialize an underlying HTTP stream.
   *
   * @param engine, handle to the engine that will manage this stream.
   * @return long, handle to the underlying stream.
   */
  protected static native long initStream(long engine);

  /**
   * Open an underlying HTTP stream. Note: Streams must be started before other other interaction
   * can can occur.
   *
   * @param stream,   handle to the stream to be started.
   * @param observer, the observer that will run the stream callbacks.
   * @return envoy_stream, with a stream handle and a success status, or a failure status.
   */
  protected static native int startStream(long stream, EnvoyObserver observer);

  /**
   * Send headers over an open HTTP stream. This method can be invoked once and needs to be called
   * before send_data.
   *
   * @param stream,    the stream to send headers over.
   * @param headers,   the headers to send.
   * @param endStream, supplies whether this is headers only.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendHeaders(long stream, EnvoyHeaders headers, boolean endStream);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple times.
   *
   * @param stream,    the stream to send data over.
   * @param data,      the data to send.
   * @param endStream, supplies whether this is the last data in the stream.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendData(long stream, EnvoyData data, boolean endStream);

  /**
   * Send metadata over an HTTP stream. This method can be invoked multiple times.
   *
   * @param stream,   the stream to send metadata over.
   * @param metadata, the metadata to send.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendMetadata(long stream, EnvoyHeaders metadata);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly ends the stream.
   *
   * @param stream,   the stream to send trailers over.
   * @param trailers, the trailers to send.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendTrailers(long stream, EnvoyHeaders trailers);

  /**
   * Detach all observers from a stream and send an interrupt upstream if supported by transport.
   *
   * @param stream, the stream to evict.
   * @return int, the resulting status of the operation.
   */
  protected static native int resetStream(long stream);

  // Native entry point

  /**
   * Initialize an engine for handling network streams.
   *
   * @return envoy_engine_t, handle to the underlying engine.
   */
  protected static native long initEngine();

  /**
   * External entry point for library.
   *
   * @param config,   the configuration blob to run envoy with.
   * @param logLevel, the logging level to run envoy with.
   * @return int, the resulting status of the operation.
   */
  protected static native int runEngine(String config, String logLevel);
}
