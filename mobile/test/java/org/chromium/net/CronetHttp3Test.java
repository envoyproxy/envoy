package org.chromium.net;

import static org.junit.Assert.*;
import static org.robolectric.Shadows.shadowOf;
import static com.google.common.truth.Truth.assertThat;

import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.NetworkCapabilities;
import android.net.Network;
import android.net.NetworkInfo;
import android.Manifest;

import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;
import io.envoyproxy.envoymobile.engine.types.EnvoyConnectionType;
import io.envoyproxy.envoymobile.engine.AndroidNetworkMonitor;
import io.envoyproxy.envoymobile.engine.AndroidNetworkMonitorV2;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import io.envoyproxy.envoymobile.engine.JniLibrary;
import org.chromium.net.impl.CronvoyUrlRequestContext;
import org.chromium.net.impl.CronvoyLogger;
import org.chromium.net.impl.NativeCronvoyEngineBuilderImpl;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.filters.SmallTest;
import androidx.test.rule.GrantPermissionRule;

import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.TestUploadDataProvider;
import org.chromium.net.testing.TestUrlRequestCallback;

import org.junit.After;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import org.robolectric.RobolectricTestRunner;
import org.robolectric.shadows.ShadowNetwork;
import org.robolectric.shadows.ShadowNetworkInfo;
import org.robolectric.shadows.ShadowNetworkCapabilities;

import java.util.HashMap;
import java.util.Map;
import java.util.Collections;
/**
 * Test CronetEngine with production HTTP/3 logic
 */
@RunWith(RobolectricTestRunner.class)
public class CronetHttp3Test {
  @Rule
  public GrantPermissionRule grantPermissionRule =
      GrantPermissionRule.grant(Manifest.permission.ACCESS_NETWORK_STATE);

  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private static final String TAG = CronetHttp3Test.class.getSimpleName();

  // URLs used for tests.

  // If true, dump envoy logs on test completion.
  // Ideally we could override this from the command line but that's TBD.
  private boolean printEnvoyLogs = false;
  // The HTTP/2 server, set up to alt-svc to the HTTP/3 server
  private HttpTestServerFactory.HttpTestServer http2TestServer;
  // The HTTP/3 server
  private HttpTestServerFactory.HttpTestServer http3TestServer;
  // An optional CronvoyLogger, set up if printEnvoyLogs is true.
  private CronvoyLogger logger;
  // The engine for this test.
  private CronvoyUrlRequestContext cronvoyEngine;
  // A URL which will point to the IP and port of the test servers.
  private String testServerUrl;
  // Optional reloadable flags to set.
  private boolean drainOnNetworkChange = false;
  private boolean resetBrokennessOnNetworkChange = false;
  private boolean disableDnsRefreshOnNetworkChange = false;
  private boolean useAndroidNetworkMonitorV2 = false;
  private ConnectivityManager connectivityManager;

  @BeforeClass
  public static void loadJniLibrary() {
    JniLibrary.loadTestLibrary();
  }

  public void setUp(boolean setUpLogging) throws Exception {
    // Set up the HTTP/3 server
    Map<String, String> headers = new HashMap<>();
    http3TestServer = HttpTestServerFactory.start(HttpTestServerFactory.Type.HTTP3, 0, headers,
                                                  "This is a simple text file served by QUIC.\n",
                                                  Collections.emptyMap());
    // Next set up the HTTP/2 server, advertising HTTP/3 support for the HTTP/3 server
    String altSvc = "h3=\":" + http3TestServer.getPort() + "\"; ma=86400";
    headers.put("alt-svc", altSvc);
    // Note that the HTTP/2 server must start on the same port as Envoy currently does not accept
    // alt-svc with differing ports. This may cause problems if this UDP port is in use at which
    // point listening on 127.0.0.N where N!=1 may improve flakiness.
    http2TestServer = HttpTestServerFactory.start(
        HttpTestServerFactory.Type.HTTP2_WITH_TLS, http3TestServer.getPort(), headers,
        "This is a simple text file served by QUIC.\n", Collections.emptyMap());
    testServerUrl = "https://" + http2TestServer.getAddress() + "/";

    // Optionally, set up logging. This will slow down the tests a bit but make debugging much
    // easier.
    if (setUpLogging) {
      logger = new CronvoyLogger() {
        @Override
        public void log(int logLevel, String message) {
          System.out.print(message);
        }
      };
    }

    // Set up the Envoy engine.
    NativeCronvoyEngineBuilderImpl nativeCronetEngineBuilder =
        new NativeCronvoyEngineBuilderImpl(ApplicationProvider.getApplicationContext());
    nativeCronetEngineBuilder.addRuntimeGuard("drain_pools_on_network_change",
                                              drainOnNetworkChange);
    nativeCronetEngineBuilder.setDisableDnsRefreshOnNetworkChange(disableDnsRefreshOnNetworkChange);

    if (setUpLogging) {
      nativeCronetEngineBuilder.setLogger(logger);
      nativeCronetEngineBuilder.setLogLevel(EnvoyEngine.LogLevel.TRACE);
    }
    if (useAndroidNetworkMonitorV2) {
      nativeCronetEngineBuilder.setUseV2NetworkMonitor(useAndroidNetworkMonitorV2);
    }
    // Make sure the handshake will work despite lack of real certs.
    nativeCronetEngineBuilder.setMockCertVerifierForTesting();
    cronvoyEngine = new CronvoyUrlRequestContext(nativeCronetEngineBuilder);
    // Clear network states in ConnectivityManager.
    cronvoyEngine.getEnvoyEngine().resetConnectivityState();

    if (useAndroidNetworkMonitorV2) {
      AndroidNetworkMonitorV2 androidNetworkMonitor = AndroidNetworkMonitorV2.getInstance();
      connectivityManager = androidNetworkMonitor.getConnectivityManager();
      // AndroidNetworkMonitorV2 registers 2 NetworkCallbacks.
      assertThat(shadowOf(connectivityManager).getNetworkCallbacks()).hasSize(2);
    } else {
      AndroidNetworkMonitor androidNetworkMonitor = AndroidNetworkMonitor.getInstance();
      connectivityManager = androidNetworkMonitor.getConnectivityManager();
    }
    NetworkCapabilities networkCapabilities =
        connectivityManager.getNetworkCapabilities(connectivityManager.getActiveNetwork());
    shadowOf(networkCapabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    // Verifies initial states of ShadowConnectivityManager.
    assertTrue(networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR));
  }

  @After
  public void tearDown() throws Exception {
    // Shut down Envoy and the test servers.
    cronvoyEngine.shutdown();
    http2TestServer.shutdown();
    if (http3TestServer != null) {
      http3TestServer.shutdown();
    }
    if (useAndroidNetworkMonitorV2) {
      AndroidNetworkMonitorV2.shutdown();
    }
  }

  private TestUrlRequestCallback doBasicGetRequest() {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronvoyEngine.newUrlRequestBuilder(testServerUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    return callback;
  }

  // Sets up a basic POST request with 4 byte body, set idempotent.
  private TestUrlRequestCallback doBasicPostRequest() {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    ExperimentalUrlRequest.Builder urlRequestBuilder =
        cronvoyEngine.newUrlRequestBuilder(testServerUrl, callback, callback.getExecutor());
    urlRequestBuilder.addHeader("content-type", "text");
    urlRequestBuilder.setHttpMethod("POST");
    urlRequestBuilder.setIdempotency(ExperimentalUrlRequest.Builder.IDEMPOTENT);
    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    urlRequestBuilder.setUploadDataProvider(dataProvider, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    return callback;
  }

  private void doInitialHttp2Request() {
    // Do a request to https://127.0.0.1:test_server_port/
    TestUrlRequestCallback callback = doBasicGetRequest();

    // Make sure the request succeeded. It should go out over HTTP/2 as it's the first
    // request and HTTP/3 support is not established.
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("h2", callback.mResponseInfo.getNegotiatedProtocol());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void basicHttp3Get() throws Exception {
    // Ideally we could override this from the command line but that's TBD.
    setUp(printEnvoyLogs);

    // Do the initial HTTP/2 request to get the alt-svc response.
    doInitialHttp2Request();

    // Set up a second request, which will hopefully go out over HTTP/3 due to alt-svc
    // advertisement.
    TestUrlRequestCallback callback = doBasicGetRequest();

    // Verify the second request used HTTP/3
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", callback.mResponseInfo.getNegotiatedProtocol());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void failToHttp2() throws Exception {
    // Ideally we could override this from the command line but that's TBD.
    setUp(printEnvoyLogs);

    // Do the initial HTTP/2 request to get the alt-svc response.
    doInitialHttp2Request();

    // Set up a second request, which will hopefully go out over HTTP/3 due to alt-svc
    // advertisement.
    TestUrlRequestCallback getCallback = doBasicGetRequest();

    // Verify the second request used HTTP/3
    assertEquals(200, getCallback.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback.mResponseInfo.getNegotiatedProtocol());

    // Now stop the HTTP/3 server.
    http3TestServer.shutdown();
    http3TestServer = null;

    // The next request will fail on HTTP2 but should succeed on HTTP/2 despite having a body.
    TestUrlRequestCallback postCallback = doBasicPostRequest();
    assertEquals(200, postCallback.mResponseInfo.getHttpStatusCode());
    assertEquals("h2", postCallback.mResponseInfo.getNegotiatedProtocol());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testRetryPostAfterHandshake() throws Exception {
    setUp(printEnvoyLogs);

    // Do the initial HTTP/2 request to get the alt-svc response.
    doInitialHttp2Request();

    // Set up a second request, which will hopefully go out over HTTP/3 due to alt-svc
    // advertisement.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronvoyEngine.newUrlRequestBuilder(testServerUrl, callback, callback.getExecutor());
    // Set the upstream to reset after the request.
    urlRequestBuilder.addHeader("reset_after_request", "yes");
    urlRequestBuilder.addHeader("content-type", "text");
    urlRequestBuilder.setHttpMethod("POST");
    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    urlRequestBuilder.setUploadDataProvider(dataProvider, callback.getExecutor());

    urlRequestBuilder.build().start();
    callback.blockForDone();

    // Both HTTP/3 and HTTP/2 servers will reset after the request.
    assertTrue(callback.mOnErrorCalled);
    // There are 3 requests - the initial HTTP/2 alt-svc request and the HTTP/3 request.
    // By default, POST requests will now retry.
    String stats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(stats.contains("cluster.base.upstream_rq_total: 3"));
  }

  // Set up to use HTTP/3, then force HTTP/3 to fail post-handshake. The request should
  // be retried on HTTP/2 and HTTP/3 will be marked broken.
  private void retryPostHandshake() throws Exception {
    // Do the initial HTTP/2 request to get the alt-svc response.
    doInitialHttp2Request();

    // Set up a second request, which will hopefully go out over HTTP/3 due to alt-svc
    // advertisement.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    ExperimentalUrlRequest.Builder urlRequestBuilder =
        cronvoyEngine.newUrlRequestBuilder(testServerUrl, callback, callback.getExecutor());
    urlRequestBuilder.addHeader("reset_after_request", "yes");
    urlRequestBuilder.addHeader("content-type", "text");
    urlRequestBuilder.setHttpMethod("POST");
    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    urlRequestBuilder.setUploadDataProvider(dataProvider, callback.getExecutor());
    // Set the request to be idempotent so Envoy knows it's safe to retry post-handshake
    urlRequestBuilder.setIdempotency(ExperimentalUrlRequest.Builder.IDEMPOTENT);

    urlRequestBuilder.build().start();
    callback.blockForDone();

    String stats = cronvoyEngine.getEnvoyEngine().dumpStats();

    // Both HTTP/3 and HTTP/2 servers will reset after the request.
    assertTrue(callback.mOnErrorCalled);
    // Unlike testNoRetryPostPostHandshake there will be 3 requests - the initial HTTP/2 alt-svc
    // request, the HTTP/3 request, and the HTTP/2 retry.
    assertTrue(stats.contains("cluster.base.upstream_rq_total: 3"));
    assertTrue(stats.contains("cluster.base.upstream_rq_retry: 1"));
    // Because H/3 was disallowed on the final retry and TCP connected, H/3 gets marked as broken.
    assertTrue(stats.contains("cluster.base.upstream_http3_broken: 1"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testRetryPostHandshake() throws Exception {
    setUp(printEnvoyLogs);

    retryPostHandshake();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void networkChangeNoDrains() throws Exception {
    // Disable dns refreshment so that the engine will attempt immediate draining.
    disableDnsRefreshOnNetworkChange = true;
    drainOnNetworkChange = false;
    setUp(printEnvoyLogs);

    // Do the initial handshake dance
    doInitialHttp2Request();

    // Do an HTTP/3 request
    TestUrlRequestCallback get1Callback = doBasicGetRequest();
    assertEquals(200, get1Callback.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", get1Callback.mResponseInfo.getNegotiatedProtocol());

    // There should be one HTTP/3 connection
    String postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 1"));

    // Force a network change
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkUnavailable();
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkChanged(2);
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkAvailable();

    // Do another HTTP/3 request
    TestUrlRequestCallback get2Callback = doBasicGetRequest();
    assertEquals(200, get2Callback.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", get2Callback.mResponseInfo.getNegotiatedProtocol());

    // There should be 2 HTTP/3 connections because the 2nd request was hashed to a different
    // connection pool. But the 1st HTTP/3 connection which is idle now shouldn't have been drained
    // or closed.
    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
    assertFalse(postStats, postStats.contains("cluster.base.upstream_cx_destroy"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void networkChangeWithDrains() throws Exception {
    // Disable dns refreshment so that the engine will attempt immediate draining.
    disableDnsRefreshOnNetworkChange = true;
    drainOnNetworkChange = true;
    setUp(printEnvoyLogs);

    // Do the initial handshake dance
    doInitialHttp2Request();

    // Do an HTTP/3 request
    TestUrlRequestCallback get1Callback = doBasicGetRequest();
    assertEquals(200, get1Callback.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", get1Callback.mResponseInfo.getNegotiatedProtocol());

    // There should be one HTTP/3 connection
    String postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats.contains("cluster.base.upstream_cx_http3_total: 1"));

    // Force a network change
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkUnavailable();
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkChanged(2);
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkAvailable();

    // Do another HTTP/3 request
    TestUrlRequestCallback get2Callback = doBasicGetRequest();
    assertEquals(200, get2Callback.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", get2Callback.mResponseInfo.getNegotiatedProtocol());

    // There should be 2 HTTP/3 connections because the 1st HTTP/3 connection which is idle now
    // should have been drained and closed.
    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
    // The 1st HTTP/3 connection and the TCP connection are both idle now, so they should have been
    // closed during draining.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_destroy: 2"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void networkChangeMonitorV2FromCellToWifi() throws Exception {
    // Disable dns refreshment so that the engine will attempt immediate draining.
    disableDnsRefreshOnNetworkChange = true;
    drainOnNetworkChange = true;
    useAndroidNetworkMonitorV2 = true;
    setUp(printEnvoyLogs);

    // Do the initial handshake dance
    doInitialHttp2Request();

    // Do a HTTP/3 request to establish a connection.
    TestUrlRequestCallback getCallback1 = doBasicGetRequest();
    assertEquals(200, getCallback1.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback1.mResponseInfo.getNegotiatedProtocol());

    // There should be one HTTP/3 connection.
    String postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats.contains("cluster.base.upstream_cx_http3_total: 1"));

    // Change from cell to newly connected WIFI network.
    NetworkInfo wifiNetworkInfo = ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED,
                                                                ConnectivityManager.TYPE_WIFI, 0,
                                                                true, NetworkInfo.State.CONNECTED);
    shadowOf(connectivityManager).setActiveNetworkInfo(wifiNetworkInfo);
    Network wifiNetwork = connectivityManager.getActiveNetwork();
    NetworkCapabilities networkCapabilities =
        connectivityManager.getNetworkCapabilities(wifiNetwork);
    shadowOf(networkCapabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(networkCapabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
    shadowOf(networkCapabilities).addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
    shadowOf(connectivityManager).setNetworkCapabilities(wifiNetwork, networkCapabilities);

    // Connected to the new network shouldn't be regarded as switching the default.
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onAvailable(wifiNetwork);
    });

    // Make another request. It should reuse the existing connection because the new network won't
    // be regarded as default.
    TestUrlRequestCallback getCallback2 = doBasicGetRequest();
    assertEquals(200, getCallback2.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback2.mResponseInfo.getNegotiatedProtocol());

    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    // The connection count should STILL be 1, proving reuse.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 1"));
    // No connections should have been destroyed.
    assertFalse(postStats, postStats.contains("cluster.base.upstream_cx_destroy:"));

    // Reported capability change should be regarded as switching the default.
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      LinkProperties link = new LinkProperties();
      callback.onLinkPropertiesChanged(wifiNetwork, link);
      callback.onCapabilitiesChanged(wifiNetwork, networkCapabilities);
    });

    // Do a 3rd HTTP/3 request. This must create a new connection.
    TestUrlRequestCallback getCallback3 = doBasicGetRequest();
    assertEquals(200, getCallback3.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback3.mResponseInfo.getNegotiatedProtocol());

    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    // Total HTTP/3 connections is now 2 (the original, now destroyed, and the new one).
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
    // The 1st HTTP/3 connection and the TCP connection are both idle now, so they should have been
    // closed during draining.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_destroy: 2"));

    // WIFI disconnected, no effect as long as the default network hasn't been switched.
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onLost(wifiNetwork);
    });

    // Do a 4th HTTP/3 request. This should reuse the existing connection.
    TestUrlRequestCallback getCallback4 = doBasicGetRequest();
    assertEquals(200, getCallback4.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback4.mResponseInfo.getNegotiatedProtocol());

    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    // Stats shouldn't have been changed.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_destroy: 2"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void networkChangeMonitorV2FromDisconnectedCellToWifi() throws Exception {
    // Disable dns refreshment so that the engine will attempt immediate draining.
    disableDnsRefreshOnNetworkChange = true;
    drainOnNetworkChange = true;
    useAndroidNetworkMonitorV2 = true;
    setUp(printEnvoyLogs);

    // Do the initial handshake dance
    doInitialHttp2Request();

    // Do a HTTP/3 request to establish a connection.
    TestUrlRequestCallback getCallback1 = doBasicGetRequest();
    assertEquals(200, getCallback1.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback1.mResponseInfo.getNegotiatedProtocol());

    // There should be one HTTP/3 connection.
    String postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats.contains("cluster.base.upstream_cx_http3_total: 1"));

    // Lost current cellular network.
    Network cellNetwork = connectivityManager.getActiveNetwork();
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onLost(cellNetwork);
    });

    // Change from the disconnected cell to newly connected WIFI network.
    NetworkInfo wifiNetworkInfo = ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED,
                                                                ConnectivityManager.TYPE_WIFI, 0,
                                                                true, NetworkInfo.State.CONNECTED);
    shadowOf(connectivityManager).setActiveNetworkInfo(wifiNetworkInfo);
    Network wifiNetwork = connectivityManager.getActiveNetwork();
    NetworkCapabilities networkCapabilities =
        connectivityManager.getNetworkCapabilities(wifiNetwork);
    shadowOf(networkCapabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(networkCapabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
    shadowOf(networkCapabilities).addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
    shadowOf(connectivityManager).setNetworkCapabilities(wifiNetwork, networkCapabilities);

    // Connected to the new network shouldn't be regarded as switching the default.
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onAvailable(wifiNetwork);
    });

    // Make another request. It should reuse the existing connection because the new network won't
    // be regarded as default.
    TestUrlRequestCallback getCallback2 = doBasicGetRequest();
    assertEquals(200, getCallback2.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback2.mResponseInfo.getNegotiatedProtocol());

    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    // The connection count should STILL be 1, proving reuse.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 1"));
    // No connections should have been destroyed.
    assertFalse(postStats, postStats.contains("cluster.base.upstream_cx_destroy:"));

    // Reported capability change should be regarded as switching the default.
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      LinkProperties link = new LinkProperties();
      callback.onLinkPropertiesChanged(wifiNetwork, link);
      callback.onCapabilitiesChanged(wifiNetwork, networkCapabilities);
    });

    // Do a 3rd HTTP/3 request. This must create a new connection.
    TestUrlRequestCallback getCallback3 = doBasicGetRequest();
    assertEquals(200, getCallback3.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback3.mResponseInfo.getNegotiatedProtocol());

    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    // Total HTTP/3 connections is now 2 (the original, now destroyed, and the new one).
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
    // The 1st HTTP/3 connection and the TCP connection are both idle now, so they should have been
    // closed during draining.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_destroy: 2"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void networkChangeMonitorV2VpnOnAndOff() throws Exception {
    // Disable dns refreshment so that the engine will attempt immediate draining.
    disableDnsRefreshOnNetworkChange = true;
    drainOnNetworkChange = true;
    useAndroidNetworkMonitorV2 = true;
    setUp(printEnvoyLogs);

    // Do the initial handshake dance
    doInitialHttp2Request();

    // Do a HTTP/3 request to establish a connection.
    TestUrlRequestCallback getCallback1 = doBasicGetRequest();
    assertEquals(200, getCallback1.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback1.mResponseInfo.getNegotiatedProtocol());

    // There should be one HTTP/3 connection.
    String postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats.contains("cluster.base.upstream_cx_http3_total: 1"));

    // A VPN network becomes available.
    NetworkInfo networkInfoVpn = ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED,
                                                               ConnectivityManager.TYPE_VPN, 0,
                                                               true, NetworkInfo.State.CONNECTED);
    Network vpnNetwork = ShadowNetwork.newInstance(2);
    shadowOf(connectivityManager).addNetwork(vpnNetwork, networkInfoVpn);
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_VPN);
    shadowOf(connectivityManager).setNetworkCapabilities(vpnNetwork, capabilities);

    // As long as VPN is available, it should be regarded as default network and trigger a default
    // network change.
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      // This should also purge the cellular network. But it's not observable to requests.
      callback.onAvailable(vpnNetwork);
    });

    // Do another HTTP/3 request. This should create a new connection as the existing one is
    // drained.
    TestUrlRequestCallback getCallback2 = doBasicGetRequest();
    assertEquals(200, getCallback2.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback2.mResponseInfo.getNegotiatedProtocol());

    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    // Total HTTP/3 connections is now 2 (the original, now destroyed, and the new one).
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
    // The 1st HTTP/3 connection and the TCP connection are both idle now, so they should have been
    // closed during draining.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_destroy: 2"));

    // The VPN becomes unavailable, the underlying cellular network should be regarded as the
    // default.
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onLost(vpnNetwork);
    });

    // Do a 3rd HTTP/3 request. This must create a new connection.
    TestUrlRequestCallback getCallback3 = doBasicGetRequest();
    assertEquals(200, getCallback3.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", getCallback3.mResponseInfo.getNegotiatedProtocol());

    postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    // Total HTTP/3 connections is now 3.
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_http3_total: 3"));
    assertTrue(postStats, postStats.contains("cluster.base.upstream_cx_destroy: 3"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void networkChangeAffectsBrokenness() throws Exception {
    resetBrokennessOnNetworkChange = true;
    setUp(printEnvoyLogs);

    // Set HTTP/3 to be marked as broken.
    retryPostHandshake();

    // From prior calls, there was one HTTP/3 connection established.
    String preStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(preStats.contains("cluster.base.upstream_cx_http3_total: 1"));

    // This should change QUIC brokenness to "failed recently".
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkUnavailable();
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkChanged(2);
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkAvailable();

    // The next request may go out over HTTP/2 or HTTP/3 (depends on who wins the race)
    // but HTTP/3 will be tried.
    doBasicGetRequest();
    String postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
  }
}
