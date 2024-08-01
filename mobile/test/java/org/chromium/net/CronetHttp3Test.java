package org.chromium.net;

import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;

import org.chromium.net.impl.CronvoyUrlRequestContext;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import org.chromium.net.impl.CronvoyLogger;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.filters.SmallTest;
import org.chromium.net.impl.CronvoyUrlRequestContext;
import org.chromium.net.impl.NativeCronvoyEngineBuilderImpl;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CronetTestFramework;
import org.chromium.net.testing.CronetTestRule.RequiresMinApi;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.TestUrlRequestCallback;
import org.chromium.net.testing.TestUrlRequestCallback.ResponseStep;
import io.envoyproxy.envoymobile.engine.JniLibrary;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import io.envoyproxy.envoymobile.engine.testing.HttpTestServerFactory;
import java.util.HashMap;
import java.util.Map;
import java.util.Collections;
import java.io.File;
import java.nio.file.Paths;
import java.nio.file.Files;

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
  boolean mPrintEnvoyLogs = false;
  // The HTTP/2 server, set up to alt-svc to the HTTP/3 server
  private HttpTestServerFactory.HttpTestServer mHttp2TestServer;
  // The HTTP/3 server
  private HttpTestServerFactory.HttpTestServer mHttp3TestServer;
  // An optional CronvoyLogger, set up if mPrintEnvoyLogs is true.
  private CronvoyLogger mLogger;
  // The log file name,  set up if mPrintEnvoyLogs is true.
  private String mLogFilename;
  // The engine for this test.
  CronvoyUrlRequestContext mCronvoyEngine;

  @BeforeClass
  public static void loadJniLibrary() {
    JniLibrary.loadTestLibrary();
  }

  public void setUp(boolean setUpLogging) throws Exception {
    // Set up the HTTP/3 server
    Map<String, String> headers = new HashMap<>();
    mHttp3TestServer = HttpTestServerFactory.start(HttpTestServerFactory.Type.HTTP3, 0, headers,
                                                   "This is a simple text file served by QUIC.\n",
                                                   Collections.emptyMap());
    // Next set up the HTTP/2 server, advertising HTTP/3 support for the HTTP/3 server
    String alt_svc = "h3=\":" + mHttp3TestServer.getPort() + "\"; ma=86400";
    headers.put("alt-svc", alt_svc);
    // Note that the HTTP/2 server must start on the same port as Envoy currently does not accept
    // alt-svc with differing ports. This may cause problems if this UDP port is in use at which
    // point listening on 127.0.0.N where N!=1 may improve flakiness.
    mHttp2TestServer = HttpTestServerFactory.start(
        HttpTestServerFactory.Type.HTTP2_WITH_TLS, mHttp3TestServer.getPort(), headers,
        "This is a simple text file served by QUIC.\n", Collections.emptyMap());

    // Optionally, set up logging. This will slow down the tests a bit but make debugging much
    // easier.
    if (setUpLogging) {
      File file = File.createTempFile("some-prefix", "file-ext");
      file.deleteOnExit();
      mLogFilename = file.getAbsolutePath() + "foo"; // Pick a path that doesn't exist.
      mLogger = new CronvoyLogger();
      mLogger.setNetLogToFile(mLogFilename);
    }
  }

  @After
  public void tearDown() throws Exception {
    // If logging is on, dump Envoy logs to System.out
    if (mPrintEnvoyLogs) {
      mLogger.stopLogging();
      byte[] bytes = Files.readAllBytes(Paths.get(mLogFilename));
      String fileContent = new String(bytes);
      System.out.println(fileContent);
    }

    // Shut down Envoy and the test servers.
    mCronvoyEngine.shutdown();
    mHttp2TestServer.shutdown();
    mHttp3TestServer.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testInitEngineAndStartRequest() throws Exception {
    // Ideally we could override this from the command line but that's TBD.
    setUp(mPrintEnvoyLogs);

    // Set up the Envoy engine.
    NativeCronvoyEngineBuilderImpl nativeCronetEngineBuilder =
        new NativeCronvoyEngineBuilderImpl(ApplicationProvider.getApplicationContext());
    if (mPrintEnvoyLogs) {
      nativeCronetEngineBuilder.setLogger(mLogger);
      nativeCronetEngineBuilder.setLogLevel(EnvoyEngine.LogLevel.TRACE);
    }
    // Make sure the handshake will work despite lack of real certs.
    nativeCronetEngineBuilder.setMockCertVerifierForTesting();
    mCronvoyEngine = new CronvoyUrlRequestContext(nativeCronetEngineBuilder);

    // Do a request to https://127.0.0.1:test_server_port/
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String mNewUrl = "https://" + mHttp2TestServer.getAddress() + "/";
    UrlRequest.Builder urlRequestBuilder =
        mCronvoyEngine.newUrlRequestBuilder(mNewUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();

    // Make sure the request succeeded. It should go out over HTTP/2 as it's the first
    // request and HTTP/3 support is not established.
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("h2", callback.mResponseInfo.getNegotiatedProtocol());

    // Set up a second request, which will hopefully go out over HTTP/3 due to alt-svc
    // advertisement.
    TestUrlRequestCallback callback2 = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder2 =
        mCronvoyEngine.newUrlRequestBuilder(mNewUrl, callback2, callback2.getExecutor());
    urlRequestBuilder2.build().start();
    callback2.blockForDone();

    // Verify the second request used HTTP/3
    assertEquals(200, callback2.mResponseInfo.getHttpStatusCode());
    assertEquals("h3", callback2.mResponseInfo.getNegotiatedProtocol());
  }
}
