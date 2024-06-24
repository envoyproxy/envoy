package io.envoyproxy.envoymobile.engine.testing;

/** An xDS test server factory. */
public class XdsTestServerFactory {
  /** The instance of {@link XdsTestServer}. */
  public static class XdsTestServer {
    private final long handle; // Used by the native code.
    private final String host;
    private final int port;

    private XdsTestServer(long handle, String host, int port) {
      this.handle = handle;
      this.host = host;
      this.port = port;
    }

    /** Starts the xDS server. */
    public native void start();

    /** Gets the xDS host. */
    public String getHost() { return host; }

    /** Gets the xDS port. */
    public int getPort() { return port; }

    /**
     * Sends a static `envoy::service::discovery::v3::DiscoveryResponse` message with the specified
     * cluster name.
     *
     * TODO(fredyw): Update to take a DiscoveryResponse proto.
     */
    public native void sendDiscoveryResponse(String clusterName);

    /** Shuts down the xDS server. */
    public native void shutdown();
  }

  static { System.loadLibrary("envoy_jni_xds_test_server_factory"); }

  /** Creates a new instance of {@link XdsTestServer}. */
  public static native XdsTestServer create();
}
