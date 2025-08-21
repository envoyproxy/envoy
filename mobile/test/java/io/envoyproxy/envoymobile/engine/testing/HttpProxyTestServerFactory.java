package io.envoyproxy.envoymobile.engine.testing;

/** An HTTP proxy test server factory. */
public final class HttpProxyTestServerFactory {
  /** The supported {@link HttpProxyTestServer} types. */
  public static class Type {
    public static final int HTTP_PROXY = 4;
    public static final int HTTPS_PROXY = 5;

    private Type() {}
  }

  /** The instance of {@link HttpProxyTestServer}. */
  public static class HttpProxyTestServer {
    private final long handle; // Used by the native code.
    private final String ipAddress;
    private final int port;

    private HttpProxyTestServer(long handle, String ipAddress, int port) {
      this.handle = handle;
      this.ipAddress = ipAddress;
      this.port = port;
    }

    /** Returns the server IP address. */
    public String getIpAddress() { return ipAddress; }

    /** Returns the server port. */
    public int getPort() { return port; }

    /** Shuts down the server. */
    public native void shutdown();
  }

  static { System.loadLibrary("envoy_jni_http_proxy_test_server_factory"); }

  /**
   * Starts the HTTP proxy server.
   *
   * @param type the value in {@link HttpProxyTestServerFactory.Type}
   */
  public static native HttpProxyTestServer start(int type);
}
