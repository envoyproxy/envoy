package io.envoyproxy.envoymobile.engine;

import android.net.ConnectivityManager;

import io.envoyproxy.envoymobile.engine.types.EnvoyEventTracker;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.Map;

public class JniLibrary {

  private static String envoyLibraryName = "envoy_jni";

  // Internal reference to helper object used to load and initialize the native
  // library.
  // Volatile to ensure double-checked locking works correctly.
  private static volatile JavaLoader loader = null;

  // Load test libraries based on the jvm_flag `envoy_jni_library_name`.
  // WARNING: This should only be used for testing.
  public static void loadTestLibrary() {
    if (System.getProperty("envoy_jni_library_name") != null) {
      envoyLibraryName = System.getProperty("envoy_jni_library_name");
    }
    load();
  }

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
    private JavaLoader() { System.loadLibrary(envoyLibraryName); }
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
   * @param engine  handle to the stream's associated engine.
   * @param stream  handle to the stream to be started.
   * @param callbacks context that contains dispatch logic to fire callbacks.
   * @param explicitFlowControl whether explicit flow control should be enabled for the stream.
   * @return a stream handle and a success status, or a failure status.
   */
  protected static native int startStream(long engine, long stream, EnvoyHTTPCallbacks callbacks,
                                          boolean explicitFlowControl);

  /**
   * Send headers over an open HTTP stream. This method can be invoked once and
   * needs to be called before send_data.
   *
   * @param engine     the stream's associated engine.
   * @param stream     the stream to send headers over.
   * @param headers    the headers to send.
   * @param endStream  supplies whether this is headers only.
   * @param idempotent indicates that the request is idempotent. When idempotent is set to true
   *                   Envoy Mobile will retry on HTTP/3 post-handshake failures.
   * @return the resulting status of the operation.
   */
  protected static native int sendHeaders(long engine, long stream,
                                          Map<String, List<String>> headers, boolean endStream,
                                          boolean idempotent);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple
   * times.
   *
   * @param engine    the stream's associated engine.
   * @param stream    the stream to send data over.
   * @param data      the data to send. It can be direct or non-direct byteBuffer.
   * @param length    the size in bytes of the data to send. 0 <= length <= data.capacity()
   * @param endStream supplies whether this is the last data in the stream.
   * @return int      the resulting status of the operation.
   */
  protected static native int sendData(long engine, long stream, ByteBuffer data, int length,
                                       boolean endStream);

  /**
   * Read data from the response stream. Returns immediately.
   * Has no effect if explicit flow control is not enabled.
   *
   * @param engine,    the stream's associated engine.
   * @param stream,    the stream.
   * @param byteCount, Maximum number of bytes that may be be passed by the next data callback.
   * @return int, the resulting status of the operation.
   */
  protected static native int readData(long engine, long stream, long byteCount);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once
   * per stream. Note that this method implicitly ends the stream.
   *
   * @param engine   the stream's associated engine.
   * @param stream   the stream to send trailers over.
   * @param trailers the trailers to send.
   * @return int, the resulting status of the operation.
   */
  protected static native int sendTrailers(long engine, long stream,
                                           Map<String, List<String>> trailers);

  /**
   * Detach all callbacks from a stream and send an interrupt upstream if
   * supported by transport.
   *
   * @param engine, the stream's associated engine.
   * @param stream, the stream to evict.
   * @return the resulting status of the operation.
   */
  protected static native int resetStream(long engine, long stream);

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
   * @param runningCallback, called when the engine finishes its async startup and begins running.
   * @param logger,          the logging interface.
   * @param eventTracker     the event tracking interface.
   * @return envoy_engine_t, handle to the underlying engine.
   */
  protected static native long initEngine(EnvoyOnEngineRunning runningCallback, EnvoyLogger logger,
                                          EnvoyEventTracker eventTracker);

  /**
   * External entry point for library.
   *
   * This should be run either with a non-empty config, or a non-zero bootstrap.
   * If a bootstrap pointer is passed, the engine will take ownership of the proto.
   *
   * @param engine,          the engine to run.
   * @param bootstrap,       a bootstrap pointer generated by createBootstrap.
   * @param logLevel,        the logging level to run envoy with.
   * @return int, the resulting status of the operation.
   */
  protected static native int runEngine(long engine, long bootstrap, String logLevel);

  /**
   * Terminate the engine.
   *
   * @param engine handle for the engine to terminate.
   */
  protected static native void terminateEngine(long engine);

  // Other native methods

  /**
   * Increment a counter with the given count.
   *
   * @param engine,  handle to the engine that owns the counter.
   * @param elements Elements of the counter stat.
   * @param tags Tags of the counter.
   * @param count Amount to add to the counter.
   * @return A status indicating if the action was successful.
   */
  protected static native int recordCounterInc(long engine, String elements, byte[][] tags,
                                               int count);

  /**
   * Retrieve the value of all active stats. Note that this function may block for some time.
   * @param engine,  handle to the engine that owns the counter.
   * @return The list of active stats and their values, or empty string of the operation failed
   */
  protected static native String dumpStats(long engine);

  /**
   * Register a platform-provided key-value store implementation.
   *
   * @param name,    unique name identifying this key-value store.
   * @param context, context containing logic necessary to invoke the key-value store.
   * @return int,    the resulting status of the operation.
   */
  protected static native int registerKeyValueStore(String name, JvmKeyValueStoreContext context);

  /**
   * Register a string accessor to get strings from the platform.
   *
   * @param accessorName, unique name identifying this accessor.
   * @param context,      context containing logic necessary to invoke the accessor.
   * @return int, the resulting status of the operation.
   */
  protected static native int registerStringAccessor(String accessorName,
                                                     JvmStringAccessorContext context);

  /**
   * Refresh DNS, and drain connections owned by this Engine.
   *
   * @param engine Handle to the engine for which to drain connections.
   */
  protected static native int resetConnectivityState(long engine);

  /**
   * A callback into the Envoy Engine when the default network is available.
   */
  protected static native void onDefaultNetworkAvailable(long engine);

  /**
   * A callback into the Envoy Engine when the default network was changed.
   */
  protected static native void onDefaultNetworkChanged(long engine, int networkType);

  /**
   * A callback into the Envoy Engine when the default network is unavailable.
   */
  protected static native void onDefaultNetworkUnavailable(long engine);

  /**
   * Update the proxy settings.
   *
   * @param engine Handle to the engine whose proxy settings should be updated.
   * @param host The proxy host.
   * @param port The proxy port.
   * @return The resulting status of the operation.
   */
  protected static native int setProxySettings(long engine, String host, int port);

  /**
   * Update the log level for all active logs
   *
   * @param logLevel The Log level to change to. Must be an integer 0-6.
   *                 See source/common/common/base_logger.h
   */
  protected static native void setLogLevel(int logLevel);

  /**
   * Mimic a call to AndroidNetworkLibrary#verifyServerCertificates from native code.
   * To be used for testing only.
   *
   * @param certChain The ASN.1 DER encoded bytes for certificates.
   * @param authType The key exchange algorithm name (e.g. RSA).
   * @param host The hostname of the server.
   * @return Android certificate verification result code.
   */
  public static native Object callCertificateVerificationFromNative(byte[][] certChain,
                                                                    byte[] authType, byte[] host);
  /**
   * Mimic a call to AndroidNetworkLibrary#addTestRootCertificate from native code.
   * To be used for testing only.
   *
   * @param cert DER encoded bytes of the certificate.
   */
  public static native void callAddTestRootCertificateFromNative(byte[] cert);

  /**
   * Mimic a call to AndroidNetworkLibrary#clearTestRootCertificate from native code.
   * To be used for testing only.
   *
   */
  public static native void callClearTestRootCertificateFromNative();

  /**
   * Given a filter name, create the proto config for adding the native filter
   *
   * @param filterName the name of the native filter
   * @return a filter config which can be passed back to createBootstrap
   */
  public static native String getNativeFilterConfig(String filterName);

  /**
   * Uses the provided fields to generate an Envoy bootstrap proto.
   * The returned pointer is "owned" by the caller until ownership is passed back to C++ via
   * runEngine.
   *
   * Returns a pointer to the bootstrap that can be used to run the engine.
   *
   */
  public static native long createBootstrap(
      long connectTimeoutSeconds, long dnsRefreshSeconds, long dnsFailureRefreshSecondsBase,
      long dnsFailureRefreshSecondsMax, long dnsQueryTimeoutSeconds, long dnsMinRefreshSeconds,
      byte[][] dnsPreresolveHostnames, boolean enableDNSCache, long dnsCacheSaveIntervalSeconds,
      int dnsNumRetries, boolean enableDrainPostDnsRefresh, boolean enableHttp3, boolean useCares,
      boolean forceV6, boolean useGro, String http3ConnectionOptions,
      String http3ClientConnectionOptions, byte[][] quicHints, byte[][] quicCanonicalSuffixes,
      boolean enableGzipDecompression, boolean enableBrotliDecompression,
      int numTimeoutsToTriggerPortMigration, boolean enableSocketTagging,
      boolean enableInterfaceBinding, long h2ConnectionKeepaliveIdleIntervalMilliseconds,
      long h2ConnectionKeepaliveTimeoutSeconds, long maxConnectionsPerHost,
      long streamIdleTimeoutSeconds, long perTryIdleTimeoutSeconds, String appVersion, String appId,
      boolean trustChainVerification, byte[][] filterChain,
      boolean enablePlatformCertificatesValidation, String upstreamTlsSni, byte[][] runtimeGuards,
      byte[][] cares_fallback_resolvers);

  /**
   * Initializes c-ares.
   * See <a
   * href="https://c-ares.org/docs/ares_library_init_android.html">ares_library_init_android</a>.
   */
  public static native void initCares(ConnectivityManager connectivityManager);

  /**
   * Returns true if the runtime feature is enabled.
   */
  public static native boolean isRuntimeFeatureEnabled(String featureName);
}
