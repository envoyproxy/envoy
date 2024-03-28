package io.envoyproxy.envoymobile.engine.testing;

import java.util.concurrent.atomic.AtomicBoolean;
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration;

/**
 * Wrapper class for test JNI functions
 */
public final class TestJni {
  private static final AtomicBoolean xdsServerRunning = new AtomicBoolean();

  /**
   * Initializes the xDS test server.
   *
   * @throws IllegalStateException if it's already started.
   */
  public static void initXdsTestServer() {
    if (xdsServerRunning.get()) {
      throw new IllegalStateException("xDS server is already running");
    }
    nativeInitXdsTestServer();
  }

  /**
   * Starts the xDS test server.
   *
   * @throws IllegalStateException if it's already started.
   */
  public static void startXdsTestServer() {
    if (!xdsServerRunning.compareAndSet(false, true)) {
      throw new IllegalStateException("xDS server is already running");
    }
    nativeStartXdsTestServer();
  }

  /**
   * Sends the `DiscoveryResponse` message in the YAML format.
   */
  public static void sendDiscoveryResponse(String yaml) {
    if (!xdsServerRunning.get()) {
      throw new IllegalStateException("xDS server is not running");
    }
    nativeSendDiscoveryResponse(yaml);
  }

  /**
   * Shutdowns the xDS test server. No-op if the server is already shutdown.
   */
  public static void shutdownXdsTestServer() {
    if (!xdsServerRunning.compareAndSet(true, false)) {
      return;
    }
    nativeShutdownXdsTestServer();
  }

  /**
   * Gets the xDS test server host.
   */
  public static String getXdsTestServerHost() { return nativeGetXdsTestServerHost(); }

  /**
   * Gets the xDS test server port.
   */
  public static int getXdsTestServerPort() { return nativeGetXdsTestServerPort(); }

  public static String createYaml(EnvoyConfiguration envoyConfiguration) {
    return nativeCreateYaml(envoyConfiguration.createBootstrap());
  }

  private static native void nativeInitXdsTestServer();

  private static native String nativeGetXdsTestServerHost();

  private static native int nativeGetXdsTestServerPort();

  private static native void nativeStartXdsTestServer();

  private static native void nativeSendDiscoveryResponse(String yaml);

  private static native int nativeShutdownXdsTestServer();

  private static native String nativeCreateYaml(long bootstrap);

  private TestJni() {}
}
