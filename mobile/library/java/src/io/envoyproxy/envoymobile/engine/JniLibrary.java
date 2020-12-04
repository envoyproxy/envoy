package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;

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

  /**
   * Register a factory for creating platform filter instances for each HTTP stream.
   *
   * @param filterName, unique name identifying this filter in the chain.
   * @param context,    context containing logic necessary to invoke a new filter instance.
   * @return int, the resulting status of the operation.
   */
  protected static native int registerFilterFactory(String filterName,
                                                    JvmFilterFactoryContext context);

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
   * @param engine,          the engine to run.
   * @param config,          the configuration blob to run envoy with.
   * @param logLevel,        the logging level to run envoy with.
   * @param onEngineRunning, called when the engine finishes its async startup and begins running.
   * @return int, the resulting status of the operation.
   */
  protected static native int runEngine(long engine, String config, String logLevel,
                                        EnvoyOnEngineRunning onEngineRunning);

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
   * Increment a counter with the given count.
   *
   * @param engine,  handle to the engine that owns the counter.
   * @param elements Elements of the counter stat.
   * @param count Amount to add to the counter.
   * @return A status indicating if the action was successful.
   */
  protected static native int recordCounterInc(long engine, String elements, int count);

  /**
   * Set a gauge of a given string of elements with the given value.
   *
   * @param engine,  handle to the engine that owns the gauge.
   * @param elements Elements of the gauge stat.
   * @param value Value to set to the gauge.
   * @return A status indicating if the action was successful.
   */
  protected static native int recordGaugeSet(long engine, String elements, int value);

  /**
   * Add the gauge with the given string of elements and by the given amount.
   *
   * @param engine,  handle to the engine that owns the gauge.
   * @param elements Elements of the gauge stat.
   * @param amount Amount to add to the gauge.
   * @return A status indicating if the action was successful.
   */
  protected static native int recordGaugeAdd(long engine, String elements, int amount);

  /**
   * Subtract from the gauge with the given string of elements and by the given amount.
   *
   * @param engine,  handle to the engine that owns the gauge.
   * @param elements Elements of the gauge stat.
   * @param amount Amount to subtract from the gauge.
   * @return A status indicating if the action was successful.
   */
  protected static native int recordGaugeSub(long engine, String elements, int amount);

  /**
   * Provides a configuration template that may be used for building platform
   * filter config chains.
   *
   * @return A template that may be used as a starting point for constructing
   *         platform filter configuration.
   */
  public static native String platformFilterTemplateString();

  /**
   * Provides a configuration template that may be used for building native
   * filter config chains.
   *
   * @return A template that may be used as a starting point for constructing
   *         native filter configuration.
   */
  public static native String nativeFilterTemplateString();

  /**
   * Register a string accessor to get strings from the platform.
   *
   * @param accessorName, unique name identifying this accessor.
   * @param context,    context containing logic necessary to invoke the accessor.
   * @return int, the resulting status of the operation.
   */
  protected static native int registerStringAccessor(String accessorName,
                                                     JvmStringAccessorContext context);
}
