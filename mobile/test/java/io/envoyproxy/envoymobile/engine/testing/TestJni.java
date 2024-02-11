package io.envoyproxy.envoymobile.engine.testing;

import java.util.concurrent.atomic.AtomicBoolean;
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration;

/**
 * Wrapper class for test JNI functions
 */
public final class TestJni {

  private static final AtomicBoolean sServerRunning = new AtomicBoolean();
  private static final AtomicBoolean xdsServerRunning = new AtomicBoolean();

  /**
   * Initializes an envoy server which will terminate cleartext CONNECT requests.
   *
   * @throws IllegalStateException if it's already started.
   */
  public static void startHttpProxyTestServer() {
    if (!sServerRunning.compareAndSet(false, true)) {
      throw new IllegalStateException("Server is already running");
    }
    nativeStartHttpProxyTestServer();
  }

  /**
   * Initializes an envoy server which will terminate encrypted CONNECT requests.
   *
   * @throws IllegalStateException if it's already started.
   */
  public static void startHttpsProxyTestServer() {
    if (!sServerRunning.compareAndSet(false, true)) {
      throw new IllegalStateException("Server is already running");
    }
    nativeStartHttpsProxyTestServer();
  }

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

  /*
   * Starts the server. Throws an {@link IllegalStateException} if already started.
   */
  public static void startHttp3TestServer() {
    if (!sServerRunning.compareAndSet(false, true)) {
      throw new IllegalStateException("Server is already running");
    }
    nativeStartHttp3TestServer();
  }

  /*
   * Starts the server. Throws an {@link IllegalStateException} if already started.
   */
  public static void startHttp2TestServer() {
    if (!sServerRunning.compareAndSet(false, true)) {
      throw new IllegalStateException("Server is already running");
    }
    nativeStartHttp2TestServer();
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
   * Shutdowns the server. No-op if the server is already shutdown.
   */
  public static void shutdownTestServer() {
    if (!sServerRunning.compareAndSet(true, false)) {
      return;
    }
    nativeShutdownTestServer();
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

  public static String getServerURL() {
    return "https://" + getServerHost() + ":" + getServerPort();
  }

  public static String getServerHost() { return "test.example.com"; }

  /**
   * Gets the xDS test server host.
   */
  public static String getXdsTestServerHost() { return nativeGetXdsTestServerHost(); }

  /**
   * Gets the xDS test server port.
   */
  public static int getXdsTestServerPort() { return nativeGetXdsTestServerPort(); }

  /**
   * Returns the server attributed port. Throws an {@link IllegalStateException} if not started.
   */
  public static int getServerPort() {
    if (!sServerRunning.get()) {
      throw new IllegalStateException("Server not started.");
    }
    return nativeGetServerPort();
  }

  public static String createYaml(EnvoyConfiguration envoyConfiguration) {
    return nativeCreateYaml(envoyConfiguration.createBootstrap());
  }

  private static native void nativeStartHttp3TestServer();

  private static native void nativeStartHttp2TestServer();

  private static native void nativeShutdownTestServer();

  private static native int nativeGetServerPort();

  private static native void nativeStartHttpProxyTestServer();

  private static native void nativeStartHttpsProxyTestServer();

  private static native void nativeInitXdsTestServer();

  private static native String nativeGetXdsTestServerHost();

  private static native int nativeGetXdsTestServerPort();

  private static native void nativeStartXdsTestServer();

  private static native void nativeSendDiscoveryResponse(String yaml);

  private static native int nativeShutdownXdsTestServer();

  private static native String nativeCreateYaml(long bootstrap);

  private TestJni() {}
}
