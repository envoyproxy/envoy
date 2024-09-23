package org.chromium.net;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;
import org.chromium.net.impl.CronvoyUrlRequestContext;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import org.chromium.net.impl.CronvoyLogger;
import androidx.test.core.app.ApplicationProvider;
import org.chromium.net.testing.TestUploadDataProvider;
import androidx.test.filters.SmallTest;

import org.chromium.net.impl.NativeCronvoyEngineBuilderImpl;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.TestUrlRequestCallback;

import io.envoyproxy.envoymobile.engine.JniLibrary;
import org.junit.After;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory;
import java.util.HashMap;
import java.util.Map;
import java.util.Collections;
/**
 * Test CronetEngine with production HTTP/3 logic
 */
@RunWith(RobolectricTestRunner.class)
public class CronetHttp3Test {
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
    nativeCronetEngineBuilder.addRuntimeGuard("reset_brokenness_on_nework_change", true);
    if (setUpLogging) {
      nativeCronetEngineBuilder.setLogger(logger);
      nativeCronetEngineBuilder.setLogLevel(EnvoyEngine.LogLevel.TRACE);
    }
    // Make sure the handshake will work despite lack of real certs.
    nativeCronetEngineBuilder.setMockCertVerifierForTesting();
    cronvoyEngine = new CronvoyUrlRequestContext(nativeCronetEngineBuilder);
  }

  @After
  public void tearDown() throws Exception {
    // Shut down Envoy and the test servers.
    cronvoyEngine.shutdown();
    http2TestServer.shutdown();
    if (http3TestServer != null) {
      http3TestServer.shutdown();
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
  public void testNoRetryPostAfterHandshake() throws Exception {
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
    // There are 2 requests - the initial HTTP/2 alt-svc request and the HTTP/3 request.
    // By default, POST requests will not retry.
    String stats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(stats.contains("cluster.base.upstream_rq_total: 2"));
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
  public void networkChangeAffectsBrokenness() throws Exception {
    setUp(printEnvoyLogs);

    // Set HTTP/3 to be marked as broken.
    retryPostHandshake();

    // From prior calls, there was one HTTP/3 connection established.
    String preStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(preStats.contains("cluster.base.upstream_cx_http3_total: 1"));

    // This should change QUIC brokenness to "failed recently".
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkUnavailable();
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkChanged(EnvoyNetworkType.WLAN);
    cronvoyEngine.getEnvoyEngine().onDefaultNetworkAvailable();

    // The next request may go out over HTTP/2 or HTTP/3 (depends on who wins the race)
    // but HTTP/3 will be tried.
    doBasicGetRequest();
    String postStats = cronvoyEngine.getEnvoyEngine().dumpStats();
    assertTrue(postStats.contains("cluster.base.upstream_cx_http3_total: 2"));
  }
}
