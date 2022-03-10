package org.chromium.net;

import static org.chromium.net.testing.CronetTestRule.SERVER_CERT_PEM;
import static org.chromium.net.testing.CronetTestRule.SERVER_KEY_PKCS8_PEM;
import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import androidx.test.filters.SmallTest;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.OnlyRunNativeCronet;
import org.chromium.net.testing.CronetTestUtil;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.Http2TestServer;
import org.chromium.net.testing.TestFilesInstaller;
import org.chromium.net.testing.TestUrlRequestCallback;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Simple test for Brotli support.
 */
@RunWith(RobolectricTestRunner.class)
public class BrotliTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private CronetEngine mCronetEngine;

  @Before
  public void setUp() throws Exception {
    TestFilesInstaller.installIfNeeded(getContext());
    assertTrue(
        Http2TestServer.startHttp2TestServer(getContext(), SERVER_CERT_PEM, SERVER_KEY_PKCS8_PEM));
  }

  @After
  public void tearDown() throws Exception {
    assertTrue(Http2TestServer.shutdownHttp2TestServer());
    if (mCronetEngine != null) {
      mCronetEngine.shutdown();
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testBrotliAdvertised() throws Exception {
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    builder.enableBrotli(true);
    CronetTestUtil.setMockCertVerifierForTesting(builder);
    mCronetEngine = builder.build();
    String url = Http2TestServer.getEchoAllHeadersUrl();
    TestUrlRequestCallback callback = startAndWaitForComplete(url);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    // TODO(carloseltuerto): also support "deflate" decompressor - Cronet does.
    assertTrue(callback.mResponseAsString.contains("accept-encoding: br,gzip"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testBrotliNotAdvertised() throws Exception {
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    CronetTestUtil.setMockCertVerifierForTesting(builder);
    mCronetEngine = builder.build();
    String url = Http2TestServer.getEchoAllHeadersUrl();
    TestUrlRequestCallback callback = startAndWaitForComplete(url);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertFalse(callback.mResponseAsString.contains("br"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testBrotliDecoded() throws Exception {
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    builder.enableBrotli(true);
    CronetTestUtil.setMockCertVerifierForTesting(builder);
    mCronetEngine = builder.build();
    String url = Http2TestServer.getServeSimpleBrotliResponse();
    TestUrlRequestCallback callback = startAndWaitForComplete(url);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    String expectedResponse = "The quick brown fox jumps over the lazy dog";
    assertEquals(expectedResponse, callback.mResponseAsString);
    // TODO(https://github.com/envoyproxy/envoy-mobile/issues/2086): uncomment this line.
    // assertEquals(callback.mResponseInfo.getAllHeaders().get("content-encoding").get(0),"br");
  }

  private TestUrlRequestCallback startAndWaitForComplete(String url) {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder =
        mCronetEngine.newUrlRequestBuilder(url, callback, callback.getExecutor());
    builder.build().start();
    callback.blockForDone();
    return callback;
  }
}
