package org.chromium.net;

import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CronetTestFramework;
import org.chromium.net.testing.CronetTestRule.OnlyRunNativeCronet;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.NativeTestServer;
import org.chromium.net.testing.TestUrlRequestCallback;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests that making a large number of requests do not lead to crashes.
 */
@RunWith(AndroidJUnit4.class)
public class CronetStressTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private CronetTestFramework mTestFramework;

  @Before
  public void setUp() throws Exception {
    mTestFramework = mTestRule.startCronetTestFramework();
    assertTrue(NativeTestServer.startNativeTestServer(getContext()));
  }

  @After
  public void tearDown() throws Exception {
    NativeTestServer.shutdownNativeTestServer();
  }

  @Test
  @LargeTest
  @OnlyRunNativeCronet
  @Feature({"Cronet"})
  public void testLargeNumberOfUploads() throws Exception {
    final int kNumRequest = 1000;
    final int kNumRequestHeaders = 100;
    final int kNumUploadBytes = 1000;
    final byte[] b = new byte[kNumUploadBytes];
    for (int i = 0; i < kNumRequest; i++) {
      TestUrlRequestCallback callback = new TestUrlRequestCallback();
      UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
          NativeTestServer.getEchoAllHeadersURL(), callback, callback.getExecutor());
      for (int j = 0; j < kNumRequestHeaders; j++) {
        builder.addHeader("header" + j, Integer.toString(j));
      }
      builder.addHeader("content-type", "useless/string");
      builder.setUploadDataProvider(UploadDataProviders.create(b, 0, kNumUploadBytes),
                                    callback.getExecutor());
      UrlRequest request = builder.build();
      request.start();
      callback.blockForDone();
      callback.shutdownExecutor();
      assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    }
  }
}
