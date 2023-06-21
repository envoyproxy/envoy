package io.envoyproxy.envoymobile.engine.testing;

import java.util.concurrent.atomic.AtomicBoolean;
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration;
import io.envoyproxy.envoymobile.engine.JniLibrary;

/**
 * Wrapper class for test JNI functions
 */
public final class TestJni {

  private static final AtomicBoolean sServerRunning = new AtomicBoolean();

  /*
   * Starts the server. Throws an {@link IllegalStateException} if already started.
   */
  public static void startHttp3TestServer() {
    if (!sServerRunning.compareAndSet(false, true)) {
      throw new IllegalStateException("Http3 server is already running");
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
   * Shutdowns the server. No-op if the server is already shutdown.
   */
  public static void shutdownTestServer() {
    if (!sServerRunning.compareAndSet(true, false)) {
      return;
    }
    nativeShutdownTestServer();
  }

  public static String getServerURL() {
    return "https://" + getServerHost() + ":" + getServerPort();
  }

  public static String getServerHost() { return "test.example.com"; }

  /**
   * Returns the server attributed port. Throws an {@link IllegalStateException} if not started.
   */
  public static int getServerPort() {
    if (!sServerRunning.get()) {
      throw new IllegalStateException("Quic server not started.");
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

  private static native String nativeCreateYaml(long bootstrap);

  private TestJni() {}
}
