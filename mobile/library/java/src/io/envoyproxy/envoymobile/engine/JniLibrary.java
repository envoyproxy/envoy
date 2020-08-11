package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.Map;

class JniLibrary {

  private static final String ENVOY_JNI = "envoy_jni";

  // Internal reference to helper object used to load and initialize the native
  // library.
  // Volatile to ensure double-checked locking works correctly.
  private static volatile JavaLoader loader = null;

  // Load and initialize Envoy and its dependencies, but only once.
  public static void load() {
    if (loader != null) {
      return;
    }

    synchronized (JavaLoader.class) {
      if (loader != null) {
        return;
      }

      loader = new JavaLoader();
    }
  }

  // Private helper class used by the load method to ensure the native library and
  // its
  // dependencies are loaded and initialized at most once.
  private static class JavaLoader {

    private JavaLoader() { System.loadLibrary(ENVOY_JNI); }
  }

  /**
   * Initialize an underlying HTTP stream.
   *
   * @param engine, handle to the engine that will manage this stream.
   * @return long, handle to the underlying stream.
   */
  protected static native long initStream(long engine);

  /**
   * Open an underlying HTTP stream. Note: Streams must be started before other
   * other interaction can can occur.
   *
   * @param stream,  handle to the stream to be started.
   * @param context, context that contains dispatch logic to fire callbacks
   *                 callbacks.
   * @return envoy_stream, with a stream handle and a success status, or a failure
   *         status.
   */
  protected static native int startStream(long stream, JvmCallbackContext context);

  /**
   * Send headers over an open HTTP stream. This method can be invoked once and
   * needs to be called before send_data.
   *
   * @param stream,    the stream to send headers over.
   * @param headers,   the headers to send.
   * @param endStream, supplies whether this is headers only.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendHeaders(long stream, byte[][] headers, boolean endStream);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple
   * times.
   *
   * @param stream,    the stream to send data over.
   * @param data,      the data to send.
   * @param endStream, supplies whether this is the last data in the stream.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendData(long stream, byte[] data, boolean endStream);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple
   * times.
   *
   * @param stream,    the stream to send data over.
   * @param data,      the data to send; must be a <b>direct</b> ByteBuffer.
   * @param endStream, supplies whether this is the last data in the stream.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendData(long stream, ByteBuffer data, boolean endStream);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once
   * per stream. Note that this method implicitly ends the stream.
   *
   * @param stream,   the stream to send trailers over.
   * @param trailers, the trailers to send.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendTrailers(long stream, byte[][] trailers);

  /**
   * Detach all callbacks from a stream and send an interrupt upstream if
   * supported by transport.
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
   * @param engine,   the engine to run.
   * @param config,   the configuration blob to run envoy with.
   * @param logLevel, the logging level to run envoy with.
   * @return int, the resulting status of the operation.
   */
  protected static native int runEngine(long engine, String config, String logLevel);

  // Other native methods

  /**
   * Provides a default configuration template that may be used for starting
   * Envoy.
   *
   * @return A template that may be used as a starting point for constructing
   *         configurations.
   */
  public static native String templateString();

  /**
   * Increment a counter.
   */
  protected static native void recordCounter(String elements, int count);
}
