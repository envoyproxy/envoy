package io.envoyproxy.envoymobile.engine.testing;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Wrapper class to start a Quic test server.
 */
public final class QuicTestServer {

  private static final AtomicBoolean sServerRunning = new AtomicBoolean();

  /*
   * Starts the server. Throws an {@link IllegalStateException} if already started.
   */
  public static void startQuicTestServer() {
    if (!sServerRunning.compareAndSet(false, true)) {
      throw new IllegalStateException("Quic server is already running");
    }
    nativeStartQuicTestServer();
  }

  /**
   * Shutdowns the server. No-op if the server is already shutdown.
   */
  public static void shutdownQuicTestServer() {
    if (!sServerRunning.compareAndSet(true, false)) {
      return;
    }
    nativeShutdownQuicTestServer();
  }

  public static String getServerURL() {
    return "https://" + getServerHost() + ":" + getServerPort() + "/";
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

  private static native void nativeStartQuicTestServer();

  private static native void nativeShutdownQuicTestServer();

  private static native int nativeGetServerPort();

  private QuicTestServer() {}
}
