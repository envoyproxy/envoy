package io.envoyproxy.envoymobile.engine.testing;

import java.util.Collections;
import java.util.Map;

/** An HTTP test server factory. */
public final class HttpTestServerFactory {
  /** The supported {@link HttpTestServer} types. */
  public static class Type {
    public static final int HTTP1_WITHOUT_TLS = 0;
    public static final int HTTP1_WITH_TLS = 1;
    public static final int HTTP2_WITH_TLS = 2;
    public static final int HTTP3 = 3;

    private Type() {}
  }

  /** The instance of {@link HttpTestServer}. */
  public static class HttpTestServer {
    private final long handle; // Used by the native code.
    private final String ipAddress;
    private final int port;
    private final String address;

    private HttpTestServer(long handle, String ipAddress, int port, String address) {
      this.handle = handle;
      this.ipAddress = ipAddress;
      this.port = port;
      this.address = address;
    }

    /** Returns the server IP address. */
    public String getIpAddress() { return ipAddress; }

    /** Returns the server port. */
    public int getPort() { return port; }

    /** Returns the combination of IP address and port number. */
    public String getAddress() { return address; }

    /** Shuts down the server. */
    public native void shutdown();
  }

  static { System.loadLibrary("envoy_jni_http_test_server_factory"); }

  /**
   * Starts the server and returns the instance of the {@link HttpTestServer}. This server will
   * always return 200 HTTP status code.
   *
   * @param type the value in {@link HttpTestServerFactory.Type}
   * @param headers the response headers
   * @param body the response body
   * @param trailers the response headers
   * @return the `HttpTestServer` instance
   */
  public static native HttpTestServer start(int type, int port, Map<String, String> headers,
                                            String body, Map<String, String> trailers);

  /**
   * A convenience method to start the server with an empty response headers, body, and trailers.
   * This server will always return 200 HTTP status code.
   *
   * @param type the value in {@link HttpTestServerFactory.Type}
   * @return the `HttpTestServer` instance
   */
  public static HttpTestServer start(int type) {
    return start(type, 0, Collections.emptyMap(), "", Collections.emptyMap());
  }
}
