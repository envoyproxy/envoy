package io.envoyproxy.envoymobile.engine.testing;

import java.util.Collections;
import java.util.Map;

/** An HTTP test server. */
public final class HttpTestServer {
  /** The supported {@link HttpTestServer} types. */
  public static class Type {
    public static final int HTTP1_WITHOUT_TLS = 0;
    public static final int HTTP2_WITH_TLS = 1;
    public static final int HTTP3 = 2;
    public static final int HTTP_PROXY = 3;
    public static final int HTTPS_PROXY = 4;

    private Type() {}
  }

  /** The instance of {@link HttpServer}. */
  public static class HttpServer {
    private final long handle; // Used by the native code.
    private final int port;

    private HttpServer(long handle, int port) {
      this.handle = handle;
      this.port = port;
    }

    /** Returns the server port. */
    public int getPort() { return port; }

    /** Shuts down the server. */
    public native void shutdown();
  }

  static { System.loadLibrary("envoy_jni_http_test_server"); }

  /**
   * Starts the server and returns the instance of the {@link HttpServer}. This server will always
   * return 200 HTTP status code.
   *
   * @param type the value in {@link HttpTestServer.Type}
   * @param headers the response headers
   * @param body the response body
   */
  public static native HttpServer start(int type, Map<String, String> headers, String body);

  /**
   * A convenience method to start the server with an empty response headers and body. This server
   * will always return 200 HTTP status code.
   *
   * @param type the value in {@link HttpTestServer.Type}
   */
  public static HttpServer start(int type) { return start(type, Collections.emptyMap(), ""); }
}
