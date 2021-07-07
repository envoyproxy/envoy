package org.chromium.net;

import static org.chromium.net.testing.CronetTestRule.assertContains;
import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import android.os.Build;
import android.os.ConditionVariable;
import android.os.StrictMode;
import android.util.Log;
import androidx.test.filters.SmallTest;
import java.io.IOException;
import java.net.ConnectException;
import java.nio.ByteBuffer;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.chromium.net.impl.Annotations.NetError;
import org.chromium.net.impl.CronetUrlRequest;
import org.chromium.net.impl.UrlResponseInfoImpl;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CronetTestFramework;
import org.chromium.net.testing.CronetTestRule.OnlyRunNativeCronet;
import org.chromium.net.testing.CronetTestRule.RequiresMinApi;
import org.chromium.net.testing.FailurePhase;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.MockUrlRequestJobFactory;
import org.chromium.net.testing.NativeTestServer;
import org.chromium.net.testing.TestUploadDataProvider;
import org.chromium.net.testing.TestUrlRequestCallback;
import org.chromium.net.testing.TestUrlRequestCallback.FailureType;
import org.chromium.net.testing.TestUrlRequestCallback.ResponseStep;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Test functionality of CronetUrlRequest.
 */
@RunWith(RobolectricTestRunner.class)
public class CronetUrlRequestTest {

  // URL used for base tests.
  private static final String TEST_URL = "http://127.0.0.1:8000";

  private static final String REFER_STRING = "refer";
  private static final String REFERRER_HEADER_NAME = REFER_STRING + "er";

  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private CronetTestFramework mTestFramework;
  private MockUrlRequestJobFactory mMockUrlRequestJobFactory;

  @Before
  public void setUp() {
    mTestFramework = mTestRule.startCronetTestFramework();
    assertTrue(NativeTestServer.startNativeTestServer(getContext()));
    // Add url interceptors after native application context is initialized.
    if (!mTestRule.testingJavaImpl()) {
      mMockUrlRequestJobFactory = new MockUrlRequestJobFactory(mTestFramework.mCronetEngine);
    }
  }

  @After
  public void tearDown() {
    if (!mTestRule.testingJavaImpl()) {
      mMockUrlRequestJobFactory.shutdown();
    }
    NativeTestServer.shutdownNativeTestServer();
  }

  private TestUrlRequestCallback startAndWaitForComplete(String url) throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    // Create request.
    UrlRequest.Builder builder =
        mTestFramework.mCronetEngine.newUrlRequestBuilder(url, callback, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.blockForDone();
    // Wait for all posted tasks to be executed to ensure there is no unhandled exception.
    callback.shutdownExecutorAndWait();
    assertTrue(urlRequest.isDone());
    return callback;
  }

  private void checkResponseInfo(UrlResponseInfo responseInfo, String expectedUrl,
                                 int expectedHttpStatusCode, String expectedHttpStatusText) {
    assertEquals(expectedUrl, responseInfo.getUrl());
    assertEquals(expectedUrl,
                 responseInfo.getUrlChain().get(responseInfo.getUrlChain().size() - 1));
    assertEquals(expectedHttpStatusCode, responseInfo.getHttpStatusCode());
    assertEquals(expectedHttpStatusText, responseInfo.getHttpStatusText());
    assertFalse(responseInfo.wasCached());
    assertTrue(responseInfo.toString().length() > 0);
  }

  private void checkResponseInfoHeader(UrlResponseInfo responseInfo, String headerName,
                                       String headerValue) {
    Map<String, List<String>> responseHeaders = responseInfo.getAllHeaders();
    List<String> header = responseHeaders.get(headerName);
    assertNotNull(header);
    assertTrue(header.contains(headerValue));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBuilderChecks() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    try {
      mTestFramework.mCronetEngine.newUrlRequestBuilder(null, callback, callback.getExecutor());
      fail("URL not null-checked");
    } catch (NullPointerException e) {
      assertEquals("URL is required.", e.getMessage());
    }
    try {
      mTestFramework.mCronetEngine.newUrlRequestBuilder(NativeTestServer.getRedirectURL(), null,
                                                        callback.getExecutor());
      fail("Callback not null-checked");
    } catch (NullPointerException e) {
      assertEquals("Callback is required.", e.getMessage());
    }
    try {
      mTestFramework.mCronetEngine.newUrlRequestBuilder(NativeTestServer.getRedirectURL(), callback,
                                                        null);
      fail("Executor not null-checked");
    } catch (NullPointerException e) {
      assertEquals("Executor is required.", e.getMessage());
    }
    // Verify successful creation doesn't throw.
    mTestFramework.mCronetEngine.newUrlRequestBuilder(NativeTestServer.getRedirectURL(), callback,
                                                      callback.getExecutor());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1540")
  public void testSimpleGet() throws Exception {
    String url = NativeTestServer.getEchoMethodURL();
    TestUrlRequestCallback callback = startAndWaitForComplete(url);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    // Default method is 'GET'.
    assertEquals("GET", callback.mResponseAsString);
    assertEquals(0, callback.mRedirectCount);
    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
    UrlResponseInfo urlResponseInfo =
        createUrlResponseInfo(new String[] {url}, "OK", 200, 86, "connection", "close",
                              "content-length", "3", "content-type", "text/plain");
    mTestRule.assertResponseEquals(urlResponseInfo, callback.mResponseInfo);
    checkResponseInfo(callback.mResponseInfo, NativeTestServer.getEchoMethodURL(), 200, "OK");
  }

  private static UrlResponseInfo createUrlResponseInfo(String[] urls, String message,
                                                       int statusCode, int receivedBytes,
                                                       String... headers) {
    ArrayList<Map.Entry<String, String>> headersList = new ArrayList<>();
    for (int i = 0; i < headers.length; i += 2) {
      headersList.add(
          new AbstractMap.SimpleImmutableEntry<String, String>(headers[i], headers[i + 1]));
    }
    UrlResponseInfoImpl unknown =
        new UrlResponseInfoImpl(Arrays.asList(urls), statusCode, message, headersList, false,
                                "unknown", ":0", receivedBytes);
    return unknown;
  }

  /**
   * Tests that disabling connection migration sets the URLRequest load flag correctly.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1522")
  public void testLoadFlagsWithConnectionMigration() throws Exception {}

  /**
   * Tests a redirect by running it step-by-step. Also tests that delaying a
   * request works as expected. To make sure there are no unexpected pending
   * messages, does a GET between UrlRequest.Callback callbacks.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1426")
  public void testRedirectAsync() throws Exception {
    // Start the request and wait to see the redirect.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setAutoAdvance(false);
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectURL(), callback, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.waitForNextStep();

    // Check the redirect.
    assertEquals(ResponseStep.ON_RECEIVED_REDIRECT, callback.mResponseStep);
    assertEquals(1, callback.mRedirectResponseInfoList.size());
    checkResponseInfo(callback.mRedirectResponseInfoList.get(0), NativeTestServer.getRedirectURL(),
                      302, "Found");
    assertEquals(1, callback.mRedirectResponseInfoList.get(0).getUrlChain().size());
    assertEquals(NativeTestServer.getSuccessURL(), callback.mRedirectUrlList.get(0));
    checkResponseInfoHeader(callback.mRedirectResponseInfoList.get(0), "redirect-header",
                            "header-value");

    UrlResponseInfo expected = createUrlResponseInfo(
        new String[] {NativeTestServer.getRedirectURL()}, "Found", 302, 73, "content-length", "92",
        "location", "/success.txt", "redirect-header", "header-value");
    mTestRule.assertResponseEquals(expected, callback.mRedirectResponseInfoList.get(0));

    // Wait for an unrelated request to finish. The request should not
    // advance until followRedirect is invoked.
    testSimpleGet();
    assertEquals(ResponseStep.ON_RECEIVED_REDIRECT, callback.mResponseStep);
    assertEquals(1, callback.mRedirectResponseInfoList.size());

    // Follow the redirect and wait for the next set of headers.
    urlRequest.followRedirect();
    callback.waitForNextStep();

    assertEquals(ResponseStep.ON_RESPONSE_STARTED, callback.mResponseStep);
    assertEquals(1, callback.mRedirectResponseInfoList.size());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    checkResponseInfo(callback.mResponseInfo, NativeTestServer.getSuccessURL(), 200, "OK");
    assertEquals(2, callback.mResponseInfo.getUrlChain().size());
    assertEquals(NativeTestServer.getRedirectURL(), callback.mResponseInfo.getUrlChain().get(0));
    assertEquals(NativeTestServer.getSuccessURL(), callback.mResponseInfo.getUrlChain().get(1));

    // Wait for an unrelated request to finish. The request should not
    // advance until read is invoked.
    testSimpleGet();
    assertEquals(ResponseStep.ON_RESPONSE_STARTED, callback.mResponseStep);

    // One read should get all the characters, but best not to depend on
    // how much is actually read from the socket at once.
    while (!callback.isDone()) {
      callback.startNextRead(urlRequest);
      callback.waitForNextStep();
      String response = callback.mResponseAsString;
      ResponseStep step = callback.mResponseStep;
      if (!callback.isDone()) {
        assertEquals(ResponseStep.ON_READ_COMPLETED, step);
      }
      // Should not receive any messages while waiting for another get,
      // as the next read has not been started.
      testSimpleGet();
      assertEquals(response, callback.mResponseAsString);
      assertEquals(step, callback.mResponseStep);
    }
    assertEquals(ResponseStep.ON_SUCCEEDED, callback.mResponseStep);
    assertEquals(NativeTestServer.SUCCESS_BODY, callback.mResponseAsString);

    UrlResponseInfo urlResponseInfo = createUrlResponseInfo(
        new String[] {NativeTestServer.getRedirectURL(), NativeTestServer.getSuccessURL()}, "OK",
        200, 258, "content-length", "20", "content-type", "text/plain",
        "access-control-allow-origin", "*", "header-name", "header-value", "multi-header-name",
        "header-value1", "multi-header-name", "header-value2");

    mTestRule.assertResponseEquals(urlResponseInfo, callback.mResponseInfo);
    // Make sure there are no other pending messages, which would trigger
    // asserts in TestUrlRequestCallback.
    testSimpleGet();
  }

  /**
   * Tests redirect without location header doesn't cause a crash.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testRedirectWithNullLocationHeader() throws Exception {
    String url = NativeTestServer.getFileURL("/redirect_broken_header.html");
    TestUrlRequestCallback callback = new TestUrlRequestCallback();

    UrlRequest.Builder builder =
        mTestFramework.mCronetEngine.newUrlRequestBuilder(url, callback, callback.getExecutor());
    final UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.blockForDone();
    assertEquals("<!DOCTYPE html>\n<html>\n<head>\n<title>Redirect</title>\n"
                     + "<p>Redirecting...</p>\n</head>\n</html>\n",
                 callback.mResponseAsString);
    assertEquals(ResponseStep.ON_SUCCEEDED, callback.mResponseStep);
    assertEquals(302, callback.mResponseInfo.getHttpStatusCode());
    assertNull(callback.mError);
    assertFalse(callback.mOnErrorCalled);
  }

  /**
   * Tests onRedirectReceived after cancel doesn't cause a crash.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testOnRedirectReceivedAfterCancel() throws Exception {
    final AtomicBoolean failedExpectation = new AtomicBoolean();
    TestUrlRequestCallback callback = new TestUrlRequestCallback() {
      @Override
      public void onRedirectReceived(UrlRequest request, UrlResponseInfo info,
                                     String newLocationUrl) {
        assertEquals(0, mRedirectCount);
        failedExpectation.compareAndSet(false, 0 != mRedirectCount);
        super.onRedirectReceived(request, info, newLocationUrl);
        // Cancel the request, so the second redirect will not be received.
        request.cancel();
      }

      @Override
      public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onReadCompleted(UrlRequest request, UrlResponseInfo info, ByteBuffer byteBuffer) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onCanceled(UrlRequest request, UrlResponseInfo info) {
        assertEquals(1, mRedirectCount);
        failedExpectation.compareAndSet(false, 1 != mRedirectCount);
        super.onCanceled(request, info);
      }
    };

    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getMultiRedirectURL(), callback, callback.getExecutor());

    final UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.blockForDone();
    assertFalse(failedExpectation.get());
    // Check that only one redirect is received.
    assertEquals(1, callback.mRedirectCount);
    // Check that onCanceled is called.
    assertTrue(callback.mOnCanceledCalled);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testNotFound() throws Exception {
    String url = NativeTestServer.getFileURL("/notfound.html");
    TestUrlRequestCallback callback = startAndWaitForComplete(url);
    checkResponseInfo(callback.mResponseInfo, url, 404, "Not Found");
    assertEquals("<!DOCTYPE html>\n<html>\n<head>\n<title>Not found</title>\n"
                     + "<p>Test page loaded.</p>\n</head>\n</html>\n",
                 callback.mResponseAsString);
    assertEquals(0, callback.mRedirectCount);
    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
  }

  // Checks that UrlRequest.Callback.onFailed is only called once in the case
  // of ERR_CONTENT_LENGTH_MISMATCH, which has an unusual failure path.
  // See http://crbug.com/468803.
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // No canonical exception to assert on
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1550")
  public void testContentLengthMismatchFailsOnce() throws Exception {
    String url = NativeTestServer.getFileURL("/content_length_mismatch.html");
    TestUrlRequestCallback callback = startAndWaitForComplete(url);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    // The entire response body will be read before the error is returned.
    // This is because the network stack returns data as it's read from the
    // socket, and the socket close message which triggers the error will
    // only be passed along after all data has been read.
    assertEquals("Response that lies about content length.", callback.mResponseAsString);
    assertNotNull(callback.mError);
    assertContains("Exception in CronetUrlRequest: net::ERR_CONTENT_LENGTH_MISMATCH",
                   callback.mError.getMessage());
    // Wait for a couple round trips to make sure there are no pending
    // onFailed messages. This test relies on checks in
    // TestUrlRequestCallback catching a second onFailed call.
    testSimpleGet();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testSetHttpMethod() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String methodName = "HEAD";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor());
    // Try to set 'null' method.
    try {
      builder.setHttpMethod(null);
      fail("Exception not thrown");
    } catch (NullPointerException e) {
      assertEquals("Method is required.", e.getMessage());
    }

    builder.setHttpMethod(methodName);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(0, callback.mHttpResponseDataLength);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBadMethod() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        TEST_URL, callback, callback.getExecutor());
    try {
      builder.setHttpMethod("bad:method!");
      builder.build().start();
      fail("IllegalArgumentException not thrown.");
    } catch (IllegalArgumentException e) {
      assertEquals("Invalid http method bad:method!", e.getMessage());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBadHeaderName() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        TEST_URL, callback, callback.getExecutor());
    try {
      builder.addHeader("header:name", "headervalue");
      builder.build().start();
      fail("IllegalArgumentException not thrown.");
    } catch (IllegalArgumentException e) {
      assertEquals("Invalid header header:name=headervalue", e.getMessage());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testAcceptEncodingIgnored() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoAllHeadersURL(), callback, callback.getExecutor());
    // This line should eventually throw an exception, once callers have migrated
    builder.addHeader("accept-encoding", "foozip");
    builder.build().start();
    callback.blockForDone();
    assertFalse(callback.mResponseAsString.contains("foozip"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBadHeaderValue() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        TEST_URL, callback, callback.getExecutor());
    try {
      builder.addHeader("headername", "bad header\r\nvalue");
      builder.build().start();
      fail("IllegalArgumentException not thrown.");
    } catch (IllegalArgumentException e) {
      assertEquals("Invalid header headername=bad header\r\nvalue", e.getMessage());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testAddHeader() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String headerName = "header-name";
    String headerValue = "header-value";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoHeaderURL(headerName), callback, callback.getExecutor());

    builder.addHeader(headerName, headerValue);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(headerValue, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Consider deleting - header values don't get dropped - this test seems bogus")
  public void testMultiRequestHeaders() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String headerName = "header-name";
    String headerValue1 = "header-value1";
    String headerValue2 = "header-value2";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoAllHeadersURL(), callback, callback.getExecutor());
    builder.addHeader(headerName, headerValue1);
    builder.addHeader(headerName, headerValue2);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    String headers = callback.mResponseAsString;
    Pattern pattern = Pattern.compile(headerName + ":\\s(.*)\\r\\n");
    Matcher matcher = pattern.matcher(headers);
    List<String> actualValues = new ArrayList<String>();
    while (matcher.find()) {
      actualValues.add(matcher.group(1));
    }
    assertEquals(1, actualValues.size());
    assertEquals("header-value2", actualValues.get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testCustomReferer_verbatim() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String refererValue = "http://example.com/";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoHeaderURL(REFERRER_HEADER_NAME), callback, callback.getExecutor());
    builder.addHeader(REFERRER_HEADER_NAME, refererValue);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(refererValue, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1551")
  public void testCustomReferer_changeToCanonical() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String refererValueNoTrailingSlash = "http://example.com";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoHeaderURL(REFERRER_HEADER_NAME), callback, callback.getExecutor());
    builder.addHeader(REFERRER_HEADER_NAME, refererValueNoTrailingSlash);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(refererValueNoTrailingSlash + "/", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1551")
  public void testCustomReferer_discardInvalid() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String invalidRefererValue = "foobar";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoHeaderURL(REFERRER_HEADER_NAME), callback, callback.getExecutor());
    builder.addHeader(REFERRER_HEADER_NAME, invalidRefererValue);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Header not found. :(", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testCustomUserAgent() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String userAgentName = "user-agent";
    String userAgentValue = "User-Agent-Value";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoHeaderURL(userAgentName), callback, callback.getExecutor());
    builder.addHeader(userAgentName, userAgentValue);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(userAgentValue, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testDefaultUserAgent() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    String headerName = "user-agent";
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoHeaderURL(headerName), callback, callback.getExecutor());
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertTrue("Default User-Agent should contain Cronet/n.n.n.n but is " +
                   callback.mResponseAsString,
               Pattern.matches(".+Cronet/\\d+\\.\\d+\\.\\d+\\.\\d+.+", callback.mResponseAsString));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testMockSuccess() throws Exception {
    TestUrlRequestCallback callback = startAndWaitForComplete(NativeTestServer.getSuccessURL());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(0, callback.mRedirectResponseInfoList.size());
    assertTrue(callback.mHttpResponseDataLength != 0);
    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
    Map<String, List<String>> responseHeaders = callback.mResponseInfo.getAllHeaders();
    assertEquals("header-value", responseHeaders.get("header-name").get(0));
    List<String> multiHeader = responseHeaders.get("multi-header-name");
    assertEquals(2, multiHeader.size());
    assertEquals("header-value1", multiHeader.get(0));
    assertEquals("header-value2", multiHeader.get(1));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testResponseHeadersList() throws Exception {
    TestUrlRequestCallback callback = startAndWaitForComplete(NativeTestServer.getSuccessURL());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    List<Map.Entry<String, String>> responseHeaders = callback.mResponseInfo.getAllHeadersAsList();

    assertEquals(responseHeaders.get(0), new AbstractMap.SimpleEntry<>("content-length", "20"));
    assertEquals(responseHeaders.get(1),
                 new AbstractMap.SimpleEntry<>("content-type", "text/plain"));
    assertEquals(responseHeaders.get(2),
                 new AbstractMap.SimpleEntry<>("access-control-allow-origin", "*"));
    assertEquals(responseHeaders.get(3),
                 new AbstractMap.SimpleEntry<>("header-name", "header-value"));
    assertEquals(responseHeaders.get(4),
                 new AbstractMap.SimpleEntry<>("multi-header-name", "header-value1"));
    assertEquals(responseHeaders.get(5),
                 new AbstractMap.SimpleEntry<>("multi-header-name", "header-value2"));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1426")
  public void testMockMultiRedirect() throws Exception {
    TestUrlRequestCallback callback =
        startAndWaitForComplete(NativeTestServer.getMultiRedirectURL());
    UrlResponseInfo mResponseInfo = callback.mResponseInfo;
    assertEquals(2, callback.mRedirectCount);
    assertEquals(200, mResponseInfo.getHttpStatusCode());
    assertEquals(2, callback.mRedirectResponseInfoList.size());

    // Check first redirect (multiredirect.html -> redirect.html)
    UrlResponseInfo firstExpectedResponseInfo = createUrlResponseInfo(
        new String[] {NativeTestServer.getMultiRedirectURL()}, "Found", 302, 76, "content-length",
        "92", "location", "/redirect.html", "redirect-header0", "header-value");
    UrlResponseInfo firstRedirectResponseInfo = callback.mRedirectResponseInfoList.get(0);
    mTestRule.assertResponseEquals(firstExpectedResponseInfo, firstRedirectResponseInfo);

    // Check second redirect (redirect.html -> success.txt)
    UrlResponseInfo secondExpectedResponseInfo = createUrlResponseInfo(
        new String[] {NativeTestServer.getMultiRedirectURL(), NativeTestServer.getRedirectURL(),
                      NativeTestServer.getSuccessURL()},
        "OK", 200, 334, "content-length", "20", "content-type", "text/plain",
        "access-control-allow-origin", "*", "header-name", "header-value", "multi-header-name",
        "header-value1", "multi-header-name", "header-value2");

    mTestRule.assertResponseEquals(secondExpectedResponseInfo, mResponseInfo);
    assertTrue(callback.mHttpResponseDataLength != 0);
    assertEquals(2, callback.mRedirectCount);
    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1426")
  public void testMockNotFound() throws Exception {
    TestUrlRequestCallback callback = startAndWaitForComplete(NativeTestServer.getNotFoundURL());
    UrlResponseInfo expected =
        createUrlResponseInfo(new String[] {NativeTestServer.getNotFoundURL()}, "Not Found", 404,
                              140, "content-length", "96");
    mTestRule.assertResponseEquals(expected, callback.mResponseInfo);
    assertTrue(callback.mHttpResponseDataLength != 0);
    assertEquals(0, callback.mRedirectCount);
    assertFalse(callback.mOnErrorCalled);
    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // Java impl doesn't support MockUrlRequestJobFactory
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testMockStartAsyncError() throws Exception {
    final int arbitraryNetError = -3;
    TestUrlRequestCallback callback = startAndWaitForComplete(
        MockUrlRequestJobFactory.getMockUrlWithFailure(FailurePhase.START, arbitraryNetError));
    assertNull(callback.mResponseInfo);
    assertNotNull(callback.mError);
    assertEquals(arbitraryNetError,
                 ((NetworkException)callback.mError).getCronetInternalErrorCode());
    assertEquals(0, callback.mRedirectCount);
    assertTrue(callback.mOnErrorCalled);
    assertEquals(ResponseStep.ON_FAILED, callback.mResponseStep);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // Java impl doesn't support MockUrlRequestJobFactory
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testMockReadDataSyncError() throws Exception {
    final int arbitraryNetError = -4;
    TestUrlRequestCallback callback = startAndWaitForComplete(
        MockUrlRequestJobFactory.getMockUrlWithFailure(FailurePhase.READ_SYNC, arbitraryNetError));
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(15, callback.mResponseInfo.getReceivedByteCount());
    assertNotNull(callback.mError);
    assertEquals(arbitraryNetError,
                 ((NetworkException)callback.mError).getCronetInternalErrorCode());
    assertEquals(0, callback.mRedirectCount);
    assertTrue(callback.mOnErrorCalled);
    assertEquals(ResponseStep.ON_FAILED, callback.mResponseStep);
  }

  /**
   * Tests that request continues when client certificate is requested.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testMockClientCertificateRequested() throws Exception {
    TestUrlRequestCallback callback =
        startAndWaitForComplete(MockUrlRequestJobFactory.getMockUrlForClientCertificateRequest());
    assertNotNull(callback.mResponseInfo);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("data", callback.mResponseAsString);
    assertEquals(0, callback.mRedirectCount);
    assertNull(callback.mError);
    assertFalse(callback.mOnErrorCalled);
  }

  /**
   * Tests that an SSL cert error will be reported via {@link UrlRequest.Callback#onFailed}.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // Java impl doesn't support MockUrlRequestJobFactory
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testMockSSLCertificateError() throws Exception {
    TestUrlRequestCallback callback =
        startAndWaitForComplete(MockUrlRequestJobFactory.getMockUrlForSSLCertificateError());
    assertNull(callback.mResponseInfo);
    assertNotNull(callback.mError);
    assertTrue(callback.mOnErrorCalled);
    assertEquals(-201, ((NetworkException)callback.mError).getCronetInternalErrorCode());
    assertContains("Exception in CronetUrlRequest: net::ERR_CERT_DATE_INVALID",
                   callback.mError.getMessage());
    assertEquals(ResponseStep.ON_FAILED, callback.mResponseStep);
  }

  /**
   * Tests that an SSL cert error with upload will be reported via {@link
   * UrlRequest.Callback#onFailed}.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // Java impl doesn't support MockUrlRequestJobFactory
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testSSLCertificateError() throws Exception {}

  /**
   * Checks that the buffer is updated correctly, when starting at an offset.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1540")
  public void testSimpleGetBufferUpdates() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setAutoAdvance(false);
    // Since the default method is "GET", the expected response body is also
    // "GET".
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.waitForNextStep();

    ByteBuffer readBuffer = ByteBuffer.allocateDirect(5);
    readBuffer.put("FOR".getBytes());
    assertEquals(3, readBuffer.position());

    // Read first two characters of the response ("GE"). It's theoretically
    // possible to need one read per character, though in practice,
    // shouldn't happen.
    while (callback.mResponseAsString.length() < 2) {
      assertFalse(callback.isDone());
      callback.startNextRead(urlRequest, readBuffer);
      callback.waitForNextStep();
    }

    // Make sure the two characters were read.
    assertEquals("GE", callback.mResponseAsString);

    // Check the contents of the entire buffer. The first 3 characters
    // should not have been changed, and the last two should be the first
    // two characters from the response.
    assertEquals("FORGE", bufferContentsToString(readBuffer, 0, 5));
    // The limit and position should be 5.
    assertEquals(5, readBuffer.limit());
    assertEquals(5, readBuffer.position());

    assertEquals(ResponseStep.ON_READ_COMPLETED, callback.mResponseStep);

    // Start reading from position 3. Since the only remaining character
    // from the response is a "T", when the read completes, the buffer
    // should contain "FORTE", with a position() of 4 and a limit() of 5.
    readBuffer.position(3);
    callback.startNextRead(urlRequest, readBuffer);
    callback.waitForNextStep();

    // Make sure all three characters of the response have now been read.
    assertEquals("GET", callback.mResponseAsString);

    // Check the entire contents of the buffer. Only the third character
    // should have been modified.
    assertEquals("FORTE", bufferContentsToString(readBuffer, 0, 5));

    // Make sure position and limit were updated correctly.
    assertEquals(4, readBuffer.position());
    assertEquals(5, readBuffer.limit());

    assertEquals(ResponseStep.ON_READ_COMPLETED, callback.mResponseStep);

    // One more read attempt. The request should complete.
    readBuffer.position(1);
    readBuffer.limit(5);
    callback.startNextRead(urlRequest, readBuffer);
    callback.waitForNextStep();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("GET", callback.mResponseAsString);
    checkResponseInfo(callback.mResponseInfo, NativeTestServer.getEchoMethodURL(), 200, "OK");

    // Check that buffer contents were not modified.
    assertEquals("FORTE", bufferContentsToString(readBuffer, 0, 5));

    // Position should not have been modified, since nothing was read.
    assertEquals(1, readBuffer.position());
    // Limit should be unchanged as always.
    assertEquals(5, readBuffer.limit());

    assertEquals(ResponseStep.ON_SUCCEEDED, callback.mResponseStep);

    // Make sure there are no other pending messages, which would trigger
    // asserts in TestUrlRequestCallback.
    testSimpleGet();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBadBuffers() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setAutoAdvance(false);
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.waitForNextStep();

    // Try to read using a full buffer.
    try {
      ByteBuffer readBuffer = ByteBuffer.allocateDirect(4);
      readBuffer.put("full".getBytes());
      urlRequest.read(readBuffer);
      fail("Exception not thrown");
    } catch (IllegalArgumentException e) {
      assertEquals("ByteBuffer is already full.", e.getMessage());
    }

    // Try to read using a non-direct buffer.
    try {
      ByteBuffer readBuffer = ByteBuffer.allocate(5);
      urlRequest.read(readBuffer);
      fail("Exception not thrown");
    } catch (IllegalArgumentException e) {
      assertEquals("byteBuffer must be a direct ByteBuffer.", e.getMessage());
    }

    // Finish the request with a direct ByteBuffer.
    callback.setAutoAdvance(true);
    ByteBuffer readBuffer = ByteBuffer.allocateDirect(5);
    urlRequest.read(readBuffer);
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("GET", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testNoIoInCancel() throws Exception {
    final TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setAutoAdvance(false);
    final UrlRequest urlRequest =
        mTestFramework.mCronetEngine
            .newUrlRequestBuilder(NativeTestServer.getEchoHeaderURL("blah-header"), callback,
                                  callback.getExecutor())
            .addHeader("blah-header", "blahblahblah")
            .build();
    urlRequest.start();
    callback.waitForNextStep();
    callback.startNextRead(urlRequest, ByteBuffer.allocateDirect(4));
    callback.waitForNextStep();
    StrictMode.ThreadPolicy oldPolicy = StrictMode.getThreadPolicy();
    StrictMode.setThreadPolicy(
        new StrictMode.ThreadPolicy.Builder().detectAll().penaltyDeath().penaltyLog().build());
    try {
      urlRequest.cancel();
    } finally {
      StrictMode.setThreadPolicy(oldPolicy);
    }
    callback.blockForDone();
    assertEquals(true, callback.mOnCanceledCalled);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUnexpectedReads() throws Exception {
    final TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setAutoAdvance(false);
    final UrlRequest urlRequest = mTestFramework.mCronetEngine
                                      .newUrlRequestBuilder(NativeTestServer.getRedirectURL(),
                                                            callback, callback.getExecutor())
                                      .build();

    // Try to read before starting request.
    try {
      callback.startNextRead(urlRequest);
      fail("Exception not thrown");
    } catch (IllegalStateException e) {
    }

    // Verify reading right after start throws an assertion. Both must be
    // invoked on the Executor thread, to prevent receiving data until after
    // startNextRead has been invoked.
    Runnable startAndRead = new Runnable() {
      @Override
      public void run() {
        urlRequest.start();
        try {
          callback.startNextRead(urlRequest);
          fail("Exception not thrown");
        } catch (IllegalStateException e) {
        }
      }
    };
    callback.getExecutor().submit(startAndRead).get();
    callback.waitForNextStep();

    assertEquals(callback.mResponseStep, ResponseStep.ON_RECEIVED_REDIRECT);
    // Try to read after the redirect.
    try {
      callback.startNextRead(urlRequest);
      fail("Exception not thrown");
    } catch (IllegalStateException e) {
    }
    urlRequest.followRedirect();
    callback.waitForNextStep();

    assertEquals(callback.mResponseStep, ResponseStep.ON_RESPONSE_STARTED);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());

    while (!callback.isDone()) {
      Runnable readTwice = new Runnable() {
        @Override
        public void run() {
          callback.startNextRead(urlRequest);
          // Try to read again before the last read completes.
          try {
            callback.startNextRead(urlRequest);
            fail("Exception not thrown");
          } catch (IllegalStateException e) {
          }
        }
      };
      callback.getExecutor().submit(readTwice).get();
      callback.waitForNextStep();
    }

    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
    assertEquals(NativeTestServer.SUCCESS_BODY, callback.mResponseAsString);

    // Try to read after request is complete.
    try {
      callback.startNextRead(urlRequest);
      fail("Exception not thrown");
    } catch (IllegalStateException e) {
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUnexpectedFollowRedirects() throws Exception {
    final TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setAutoAdvance(false);
    final UrlRequest urlRequest = mTestFramework.mCronetEngine
                                      .newUrlRequestBuilder(NativeTestServer.getRedirectURL(),
                                                            callback, callback.getExecutor())
                                      .build();

    // Try to follow a redirect before starting the request.
    try {
      urlRequest.followRedirect();
      fail("Exception not thrown");
    } catch (IllegalStateException e) {
    }

    // Try to follow a redirect just after starting the request. Has to be
    // done on the executor thread to avoid a race.
    Runnable startAndRead = new Runnable() {
      @Override
      public void run() {
        urlRequest.start();
        try {
          urlRequest.followRedirect();
          fail("Exception not thrown");
        } catch (IllegalStateException e) {
        }
      }
    };
    callback.getExecutor().execute(startAndRead);
    callback.waitForNextStep();

    assertEquals(callback.mResponseStep, ResponseStep.ON_RECEIVED_REDIRECT);
    // Try to follow the redirect twice. Second attempt should fail.
    Runnable followRedirectTwice = new Runnable() {
      @Override
      public void run() {
        urlRequest.followRedirect();
        try {
          urlRequest.followRedirect();
          fail("Exception not thrown");
        } catch (IllegalStateException e) {
        }
      }
    };
    callback.getExecutor().execute(followRedirectTwice);
    callback.waitForNextStep();

    assertEquals(callback.mResponseStep, ResponseStep.ON_RESPONSE_STARTED);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());

    while (!callback.isDone()) {
      try {
        urlRequest.followRedirect();
        fail("Exception not thrown");
      } catch (IllegalStateException e) {
      }
      callback.startNextRead(urlRequest);
      callback.waitForNextStep();
    }

    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
    assertEquals(NativeTestServer.SUCCESS_BODY, callback.mResponseAsString);

    // Try to follow redirect after request is complete.
    try {
      urlRequest.followRedirect();
      fail("Exception not thrown");
    } catch (IllegalStateException e) {
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadSetDataProvider() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    try {
      builder.setUploadDataProvider(null, callback.getExecutor());
      fail("Exception not thrown");
    } catch (NullPointerException e) {
      assertEquals("Invalid UploadDataProvider.", e.getMessage());
    }

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    try {
      builder.build().start();
      fail("Exception not thrown");
    } catch (IllegalArgumentException e) {
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadEmptyBodySync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();

    assertEquals(0, dataProvider.getUploadedLength());
    assertEquals(0, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("", callback.mResponseAsString);
    dataProvider.assertClosed();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadSync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(4, dataProvider.getUploadedLength());
    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("test", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadMultiplePiecesSync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("Y".getBytes());
    dataProvider.addRead("et ".getBytes());
    dataProvider.addRead("another ".getBytes());
    dataProvider.addRead("test".getBytes());

    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(16, dataProvider.getUploadedLength());
    assertEquals(4, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Yet another test", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadMultiplePiecesAsync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.ASYNC, callback.getExecutor());
    dataProvider.addRead("Y".getBytes());
    dataProvider.addRead("et ".getBytes());
    dataProvider.addRead("another ".getBytes());
    dataProvider.addRead("test".getBytes());

    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(16, dataProvider.getUploadedLength());
    assertEquals(4, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Yet another test", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadChangesDefaultMethod() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("POST", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadWithSetMethod() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor());

    final String method = "PUT";
    builder.setHttpMethod(method);

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("PUT", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadRedirectSync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    // 1 read call before the rewind, 1 after.
    assertEquals(2, dataProvider.getNumReadCalls());
    assertEquals(1, dataProvider.getNumRewindCalls());

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("test", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadRedirectAsync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.ASYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    dataProvider.assertClosed();
    callback.blockForDone();

    // 1 read call before the rewind, 1 after.
    assertEquals(2, dataProvider.getNumReadCalls());
    assertEquals(1, dataProvider.getNumRewindCalls());

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("test", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadWithBadLength() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor()) {
      @Override
      public long getLength() throws IOException {
        return 1;
      }

      @Override
      public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) throws IOException {
        byteBuffer.put("12".getBytes());
        uploadDataSink.onReadSucceeded(false);
      }
    };
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Read upload data length 2 exceeds expected length 1",
                   callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadWithBadLengthBufferAligned() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor()) {
      @Override
      public long getLength() throws IOException {
        return 8191;
      }

      @Override
      public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) throws IOException {
        byteBuffer.put("0123456789abcdef".getBytes());
        uploadDataSink.onReadSucceeded(false);
      }
    };
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();
    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Read upload data length 8192 exceeds expected length 8191",
                   callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadReadFailSync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.setReadFailure(0, TestUploadDataProvider.FailMode.CALLBACK_SYNC);
    // This will never be read, but if the length is 0, read may never be
    // called.
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Sync read failure", callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadLengthFailSync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.setLengthFailure();
    // This will never be read, but if the length is 0, read may never be
    // called.
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(0, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Sync length failure", callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadReadFailAsync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.setReadFailure(0, TestUploadDataProvider.FailMode.CALLBACK_ASYNC);
    // This will never be read, but if the length is 0, read may never be
    // called.
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Async read failure", callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  /** This test uses a direct executor for upload, and non direct for callbacks */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testDirectExecutorUploadProhibitedByDefault() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    Executor myExecutor = new Executor() {
      @Override
      public void execute(Runnable command) {
        command.run();
      }
    };
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider =
        new TestUploadDataProvider(TestUploadDataProvider.SuccessCallbackMode.SYNC, myExecutor);
    // This will never be read, but if the length is 0, read may never be
    // called.
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, myExecutor);
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();

    assertEquals(0, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Inline execution is prohibited for this request",
                   callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  /** This test uses a direct executor for callbacks, and non direct for upload */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testDirectExecutorProhibitedByDefault() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    Executor myExecutor = new Executor() {
      @Override
      public void execute(Runnable command) {
        command.run();
      }
    };
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, myExecutor);

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    // This will never be read, but if the length is 0, read may never be
    // called.
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();

    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertContains("Exception posting task to executor", callback.mError.getMessage());
    assertContains("Inline execution is prohibited for this request",
                   callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
    dataProvider.assertClosed();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testDirectExecutorAllowed() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setAllowDirectExecutor(true);
    Executor myExecutor = new Executor() {
      @Override
      public void execute(Runnable command) {
        command.run();
      }
    };
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, myExecutor);
    UploadDataProvider dataProvider = UploadDataProviders.create("test".getBytes());
    builder.setUploadDataProvider(dataProvider, myExecutor);
    builder.addHeader("content-type", "useless/string");
    builder.allowDirectExecutor();
    builder.build().start();
    callback.blockForDone();

    if (callback.mOnErrorCalled) {
      throw callback.mError;
    }

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("test", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadReadFailThrown() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.setReadFailure(0, TestUploadDataProvider.FailMode.THROWN);
    // This will never be read, but if the length is 0, read may never be
    // called.
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(0, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Thrown read failure", callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadRewindFailSync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.setRewindFailure(TestUploadDataProvider.FailMode.CALLBACK_SYNC);
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(1, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Sync rewind failure", callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadRewindFailAsync() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.ASYNC, callback.getExecutor());
    dataProvider.setRewindFailure(TestUploadDataProvider.FailMode.CALLBACK_ASYNC);
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(1, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Async rewind failure", callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadRewindFailThrown() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.setRewindFailure(TestUploadDataProvider.FailMode.THROWN);
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals(1, dataProvider.getNumRewindCalls());

    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("Thrown rewind failure", callback.mError.getCause().getMessage());
    assertEquals(null, callback.mResponseInfo);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadChunked() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test hello".getBytes());
    dataProvider.setChunked(true);
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");

    assertEquals(-1, dataProvider.getUploadedLength());

    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    // 1 read call for one data chunk.
    assertEquals(1, dataProvider.getNumReadCalls());
    assertEquals("test hello", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadChunkedLastReadZeroLengthBody() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    // Add 3 reads. The last read has a 0-length body.
    dataProvider.addRead("hello there".getBytes());
    dataProvider.addRead("!".getBytes());
    dataProvider.addRead("".getBytes());
    dataProvider.setChunked(true);
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");

    assertEquals(-1, dataProvider.getUploadedLength());

    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    // 2 read call for the first two data chunks, and 1 for final chunk.
    assertEquals(3, dataProvider.getNumReadCalls());
    assertEquals("hello there!", callback.mResponseAsString);
  }

  // Test where an upload fails without ever initializing the
  // UploadDataStream, because it can't connect to the server.
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1550")
  public void testUploadFailsWithoutInitializingStream() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    // The port for PTP will always refuse a TCP connection
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        "http://127.0.0.1:319", callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertNull(callback.mResponseInfo);
    if (mTestRule.testingJavaImpl()) {
      Throwable cause = callback.mError.getCause();
      assertTrue("Exception was: " + cause, cause instanceof ConnectException);
    } else {
      assertContains("Exception in CronetUrlRequest: net::ERR_CONNECTION_REFUSED",
                     callback.mError.getMessage());
    }
  }

  private void throwOrCancel(FailureType failureType, ResponseStep failureStep,
                             boolean expectResponseInfo, boolean expectError) {
    if (Log.isLoggable("TESTING", Log.VERBOSE)) {
      Log.v("TESTING", "Testing " + failureType + " during " + failureStep);
    }
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    callback.setFailure(failureType, failureStep);
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectURL(), callback, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.blockForDone();
    // Wait for all posted tasks to be executed to ensure there is no unhandled exception.
    callback.shutdownExecutorAndWait();
    assertEquals(1, callback.mRedirectCount);
    if (failureType == FailureType.CANCEL_SYNC || failureType == FailureType.CANCEL_ASYNC) {
      assertResponseStepCanceled(callback);
    } else if (failureType == FailureType.THROW_SYNC) {
      assertEquals(ResponseStep.ON_FAILED, callback.mResponseStep);
    }
    assertTrue(urlRequest.isDone());
    assertEquals(expectResponseInfo, callback.mResponseInfo != null);
    assertEquals(expectError, callback.mError != null);
    assertEquals(expectError, callback.mOnErrorCalled);
    // When failureType is FailureType.CANCEL_ASYNC_WITHOUT_PAUSE and failureStep is
    // ResponseStep.ON_READ_COMPLETED, there might be an onSucceeded() task already posted. If
    // that's the case, onCanceled() will not be invoked. See crbug.com/657415.
    if (!(failureType == FailureType.CANCEL_ASYNC_WITHOUT_PAUSE &&
          failureStep == ResponseStep.ON_READ_COMPLETED)) {
      assertEquals(failureType == FailureType.CANCEL_SYNC ||
                       failureType == FailureType.CANCEL_ASYNC ||
                       failureType == FailureType.CANCEL_ASYNC_WITHOUT_PAUSE,
                   callback.mOnCanceledCalled);
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testFailures() throws Exception {
    throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_RECEIVED_REDIRECT, false, false);
    throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_RECEIVED_REDIRECT, false, false);
    throwOrCancel(FailureType.CANCEL_ASYNC_WITHOUT_PAUSE, ResponseStep.ON_RECEIVED_REDIRECT, false,
                  false);
    throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_RECEIVED_REDIRECT, false, true);

    throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_RESPONSE_STARTED, true, false);
    throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_RESPONSE_STARTED, true, false);
    throwOrCancel(FailureType.CANCEL_ASYNC_WITHOUT_PAUSE, ResponseStep.ON_RESPONSE_STARTED, true,
                  false);
    throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_RESPONSE_STARTED, true, true);

    throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_READ_COMPLETED, true, false);
    throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_READ_COMPLETED, true, false);
    throwOrCancel(FailureType.CANCEL_ASYNC_WITHOUT_PAUSE, ResponseStep.ON_READ_COMPLETED, true,
                  false);
    throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_READ_COMPLETED, true, true);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testThrowOrCancelInOnSucceeded() {
    FailureType[] testTypes = new FailureType[] {FailureType.THROW_SYNC, FailureType.CANCEL_SYNC,
                                                 FailureType.CANCEL_ASYNC};
    for (FailureType type : testTypes) {
      TestUrlRequestCallback callback = new TestUrlRequestCallback();
      callback.setFailure(type, ResponseStep.ON_SUCCEEDED);
      UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
          NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor());
      UrlRequest urlRequest = builder.build();
      urlRequest.start();
      callback.blockForDone();
      // Wait for all posted tasks to be executed to ensure there is no unhandled exception.
      callback.shutdownExecutorAndWait();
      assertNull(callback.mError);
      assertEquals(ResponseStep.ON_SUCCEEDED, callback.mResponseStep);
      assertTrue(urlRequest.isDone());
      assertNotNull(callback.mResponseInfo);
      assertFalse(callback.mOnErrorCalled);
      assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
      assertEquals("GET", callback.mResponseAsString);
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testThrowOrCancelInOnFailed() {
    FailureType[] testTypes = new FailureType[] {FailureType.THROW_SYNC, FailureType.CANCEL_SYNC,
                                                 FailureType.CANCEL_ASYNC};
    for (FailureType type : testTypes) {
      String url = NativeTestServer.getEchoBodyURL();
      // Shut down NativeTestServer so request will fail.
      NativeTestServer.shutdownNativeTestServer();
      TestUrlRequestCallback callback = new TestUrlRequestCallback();
      callback.setFailure(type, ResponseStep.ON_FAILED);
      UrlRequest.Builder builder =
          mTestFramework.mCronetEngine.newUrlRequestBuilder(url, callback, callback.getExecutor());
      UrlRequest urlRequest = builder.build();
      urlRequest.start();
      callback.blockForDone();
      // Wait for all posted tasks to be executed to ensure there is no unhandled exception.
      callback.shutdownExecutorAndWait();
      assertEquals(ResponseStep.ON_FAILED, callback.mResponseStep);
      assertTrue(callback.mOnErrorCalled);
      assertNotNull(callback.mError);
      assertTrue(urlRequest.isDone());
      // Start NativeTestServer again to run the test for a second time.
      assertTrue(NativeTestServer.startNativeTestServer(getContext()));
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testThrowOrCancelInOnCanceled() {
    FailureType[] testTypes = new FailureType[] {FailureType.THROW_SYNC, FailureType.CANCEL_SYNC,
                                                 FailureType.CANCEL_ASYNC};
    for (FailureType type : testTypes) {
      TestUrlRequestCallback callback = new TestUrlRequestCallback() {
        @Override
        public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
          super.onResponseStarted(request, info);
          request.cancel();
        }
      };
      callback.setFailure(type, ResponseStep.ON_CANCELED);
      UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
          NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());
      UrlRequest urlRequest = builder.build();
      urlRequest.start();
      callback.blockForDone();
      // Wait for all posted tasks to be executed to ensure there is no unhandled exception.
      callback.shutdownExecutorAndWait();
      assertResponseStepCanceled(callback);
      assertTrue(urlRequest.isDone());
      assertNotNull(callback.mResponseInfo);
      assertNull(callback.mError);
      assertTrue(callback.mOnCanceledCalled);
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // No destroyed callback for tests
  @Ignore("Not yet implemented")
  public void testExecutorShutdown() {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();

    callback.setAutoAdvance(false);
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());
    CronetUrlRequest urlRequest = (CronetUrlRequest)builder.build();
    urlRequest.start();
    callback.waitForNextStep();
    assertFalse(callback.isDone());
    assertFalse(urlRequest.isDone());

    final ConditionVariable requestDestroyed = new ConditionVariable(false);
    // urlRequest.setOnDestroyedCallbackForTesting(new Runnable() {
    //     @Override
    //     public void run() {
    //         requestDestroyed.open();
    //     }
    // });

    // Shutdown the executor, so posting the task will throw an exception.
    callback.shutdownExecutor();
    ByteBuffer readBuffer = ByteBuffer.allocateDirect(5);
    urlRequest.read(readBuffer);
    // Callback will never be called again because executor is shutdown,
    // but request will be destroyed from network thread.
    requestDestroyed.block();

    assertFalse(callback.isDone());
    assertTrue(urlRequest.isDone());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testUploadExecutorShutdown() throws Exception {
    class HangingUploadDataProvider extends UploadDataProvider {
      UploadDataSink mUploadDataSink;
      ByteBuffer mByteBuffer;
      ConditionVariable mReadCalled = new ConditionVariable(false);

      @Override
      public long getLength() {
        return 69;
      }

      @Override
      public void read(final UploadDataSink uploadDataSink, final ByteBuffer byteBuffer) {
        mUploadDataSink = uploadDataSink;
        mByteBuffer = byteBuffer;
        mReadCalled.open();
      }

      @Override
      public void rewind(final UploadDataSink uploadDataSink) {}
    }

    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    ExecutorService uploadExecutor = Executors.newSingleThreadExecutor();
    HangingUploadDataProvider dataProvider = new HangingUploadDataProvider();
    builder.setUploadDataProvider(dataProvider, uploadExecutor);
    builder.addHeader("content-type", "useless/string");
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    // Wait for read to be called on executor.
    dataProvider.mReadCalled.block();
    // Shutdown the executor, so posting next task will throw an exception.
    uploadExecutor.shutdown();
    // Continue the upload.
    dataProvider.mByteBuffer.putInt(42);
    dataProvider.mUploadDataSink.onReadSucceeded(false);
    // Callback.onFailed will be called on request executor even though upload
    // executor is shutdown.
    callback.blockForDone();
    assertTrue(callback.isDone());
    assertTrue(callback.mOnErrorCalled);
    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertTrue(urlRequest.isDone());
  }

  /**
   * A TestUrlRequestCallback that shuts down executor upon receiving onSucceeded callback.
   */
  private static class QuitOnSuccessCallback extends TestUrlRequestCallback {
    @Override
    public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
      // Stop accepting new tasks.
      shutdownExecutor();
      super.onSucceeded(request, info);
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // No adapter to destroy in pure java
  @Ignore("Not yet implemented")
  public void testDestroyUploadDataStreamAdapterOnSucceededCallback() throws Exception {
    TestUrlRequestCallback callback = new QuitOnSuccessCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    CronetUrlRequest request = (CronetUrlRequest)builder.build();
    final ConditionVariable uploadDataStreamAdapterDestroyed = new ConditionVariable();
    // request.setOnDestroyedUploadCallbackForTesting(new Runnable() {
    //     @Override
    //     public void run() {
    //         uploadDataStreamAdapterDestroyed.open();
    //     }
    // });

    request.start();
    uploadDataStreamAdapterDestroyed.block();
    callback.blockForDone();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("", callback.mResponseAsString);
  }

  /*
   * Verifies error codes are passed through correctly.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // Java impl doesn't support MockUrlRequestJobFactory
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testErrorCodes() throws Exception {
    checkSpecificErrorCode(-105, NetworkException.ERROR_HOSTNAME_NOT_RESOLVED, "NAME_NOT_RESOLVED",
                           false);
    checkSpecificErrorCode(-106, NetworkException.ERROR_INTERNET_DISCONNECTED,
                           "INTERNET_DISCONNECTED", false);
    checkSpecificErrorCode(-21, NetworkException.ERROR_NETWORK_CHANGED, "NETWORK_CHANGED", true);
    checkSpecificErrorCode(-100, NetworkException.ERROR_CONNECTION_CLOSED, "CONNECTION_CLOSED",
                           true);
    checkSpecificErrorCode(-102, NetworkException.ERROR_CONNECTION_REFUSED, "CONNECTION_REFUSED",
                           false);
    checkSpecificErrorCode(-101, NetworkException.ERROR_CONNECTION_RESET, "CONNECTION_RESET", true);
    checkSpecificErrorCode(-118, NetworkException.ERROR_CONNECTION_TIMED_OUT,
                           "CONNECTION_TIMED_OUT", true);
    checkSpecificErrorCode(-7, NetworkException.ERROR_TIMED_OUT, "TIMED_OUT", true);
    checkSpecificErrorCode(-109, NetworkException.ERROR_ADDRESS_UNREACHABLE, "ADDRESS_UNREACHABLE",
                           false);
    checkSpecificErrorCode(-2, NetworkException.ERROR_OTHER, "FAILED", false);
  }

  /*
   * Verifies no cookies are saved or sent by default.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testCookiesArentSavedOrSent() throws Exception {
    // Make a request to a url that sets the cookie
    String url = NativeTestServer.getFileURL("/set_cookie.html");
    TestUrlRequestCallback callback = startAndWaitForComplete(url);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("A=B", callback.mResponseInfo.getAllHeaders().get("Set-Cookie").get(0));

    // Make a request that check that cookie header isn't sent.
    String headerName = "Cookie";
    String url2 = NativeTestServer.getEchoHeaderURL(headerName);
    TestUrlRequestCallback callback2 = startAndWaitForComplete(url2);
    assertEquals(200, callback2.mResponseInfo.getHttpStatusCode());
    assertEquals("Header not found. :(", callback2.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testQuicErrorCode() throws Exception {
    TestUrlRequestCallback callback =
        startAndWaitForComplete(MockUrlRequestJobFactory.getMockUrlWithFailure(
            FailurePhase.START, NetError.ERR_QUIC_PROTOCOL_ERROR));
    assertNull(callback.mResponseInfo);
    assertNotNull(callback.mError);
    assertEquals(NetworkException.ERROR_QUIC_PROTOCOL_FAILED,
                 ((NetworkException)callback.mError).getErrorCode());
    assertTrue(callback.mError instanceof QuicException);
    QuicException quicException = (QuicException)callback.mError;
    // 1 is QUIC_INTERNAL_ERROR
    assertEquals(1, quicException.getQuicDetailedErrorCode());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testQuicErrorCodeForNetworkChanged() throws Exception {
    TestUrlRequestCallback callback =
        startAndWaitForComplete(MockUrlRequestJobFactory.getMockUrlWithFailure(
            FailurePhase.START, NetError.ERR_NETWORK_CHANGED));
    assertNull(callback.mResponseInfo);
    assertNotNull(callback.mError);
    assertEquals(NetworkException.ERROR_NETWORK_CHANGED,
                 ((NetworkException)callback.mError).getErrorCode());
    assertTrue(callback.mError instanceof QuicException);
    QuicException quicException = (QuicException)callback.mError;
    // QUIC_CONNECTION_MIGRATION_NO_NEW_NETWORK(83) is set in
    // URLRequestFailedJob::PopulateNetErrorDetails for this test.
    final int quicErrorCode = 83;
    assertEquals(quicErrorCode, quicException.getQuicDetailedErrorCode());
  }

  /**
   * Tests that legacy onFailed callback is invoked with UrlRequestException if there
   * is no onFailed callback implementation that takes CronetException.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1549")
  public void testLegacyOnFailedCallback() throws Exception {
    final int netError = -123;
    final AtomicBoolean failedExpectation = new AtomicBoolean();
    final ConditionVariable done = new ConditionVariable();
    UrlRequest.Callback callback = new UrlRequest.Callback() {
      @Override
      public void onRedirectReceived(UrlRequest request, UrlResponseInfo info,
                                     String newLocationUrl) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onReadCompleted(UrlRequest request, UrlResponseInfo info, ByteBuffer byteBuffer) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
        failedExpectation.set(true);
        fail();
      }

      @Override
      public void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error) {
        assertTrue(error instanceof NetworkException);
        assertEquals(netError, ((NetworkException)error).getCronetInternalErrorCode());
        failedExpectation.set(((NetworkException)error).getCronetInternalErrorCode() != netError);
        done.open();
      }

      @Override
      public void onCanceled(UrlRequest request, UrlResponseInfo info) {
        failedExpectation.set(true);
        fail();
      }
    };

    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        MockUrlRequestJobFactory.getMockUrlWithFailure(FailurePhase.START, netError), callback,
        Executors.newSingleThreadExecutor());
    final UrlRequest urlRequest = builder.build();
    urlRequest.start();
    done.block();
    // Check that onFailed is called.
    assertFalse(failedExpectation.get());
  }

  private void checkSpecificErrorCode(int netError, int errorCode, String name,
                                      boolean immediatelyRetryable) throws Exception {
    TestUrlRequestCallback callback = startAndWaitForComplete(
        MockUrlRequestJobFactory.getMockUrlWithFailure(FailurePhase.START, netError));
    assertNull(callback.mResponseInfo);
    assertNotNull(callback.mError);
    assertEquals(netError, ((NetworkException)callback.mError).getCronetInternalErrorCode());
    assertEquals(errorCode, ((NetworkException)callback.mError).getErrorCode());
    assertEquals(immediatelyRetryable, ((NetworkException)callback.mError).immediatelyRetryable());
    assertContains("Exception in CronetUrlRequest: net::ERR_" + name, callback.mError.getMessage());
    assertEquals(0, callback.mRedirectCount);
    assertTrue(callback.mOnErrorCalled);
    assertEquals(ResponseStep.ON_FAILED, callback.mResponseStep);
  }

  // Returns the contents of byteBuffer, from its position() to its limit(),
  // as a String. Does not modify byteBuffer's position().
  private String bufferContentsToString(ByteBuffer byteBuffer, int start, int end) {
    // Use a duplicate to avoid modifying byteBuffer.
    ByteBuffer duplicate = byteBuffer.duplicate();
    duplicate.position(start);
    duplicate.limit(end);
    byte[] contents = new byte[duplicate.remaining()];
    duplicate.get(contents);
    return new String(contents);
  }

  private void assertResponseStepCanceled(TestUrlRequestCallback callback) {
    if (callback.mResponseStep == ResponseStep.ON_FAILED && callback.mError != null) {
      throw new Error("Unexpected response state: " + ResponseStep.ON_FAILED, callback.mError);
    }
    assertEquals(ResponseStep.ON_CANCELED, callback.mResponseStep);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1550")
  public void testCleartextTrafficBlocked() throws Exception {
    // This feature only works starting from N.
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      final int cleartextNotPermitted = -29;
      // This hostname needs to match the one in network_security_config.xml and the one used
      // by QuicTestServer.
      // https requests to it are tested in QuicTest, so this checks that we're only blocking
      // cleartext.
      final String url = "http://example.com/simple.txt";
      TestUrlRequestCallback callback = startAndWaitForComplete(url);
      assertNull(callback.mResponseInfo);
      assertNotNull(callback.mError);
      assertEquals(cleartextNotPermitted,
                   ((NetworkException)callback.mError).getCronetInternalErrorCode());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  /**
   * Open many connections and cancel them right away. This test verifies all internal
   * sockets and other Closeables are properly closed. See crbug.com/726193.
   */
  public void testGzipCancel() throws Exception {
    String url = NativeTestServer.getFileURL("/gzipped.html");
    for (int i = 0; i < 100; i++) {
      TestUrlRequestCallback callback = new TestUrlRequestCallback();
      callback.setAutoAdvance(false);
      UrlRequest urlRequest =
          mTestFramework.mCronetEngine.newUrlRequestBuilder(url, callback, callback.getExecutor())
              .build();
      urlRequest.start();
      urlRequest.cancel();
      // If the test blocks until each UrlRequest finishes before starting the next UrlRequest
      // then it never catches the leak. If it starts all UrlRequests and then blocks until
      // all UrlRequests finish, it only catches the leak ~10% of the time. In its current
      // form it appears to catch the leak ~70% of the time.
      // Catching the leak may require a lot of busy threads so that the cancel() happens
      // before the UrlRequest has made much progress (and set mCurrentUrlConnection and
      // mResponseChannel). This may be why blocking until each UrlRequest finishes doesn't
      // catch the leak.
      // The other quirk of this is that from teardown(), JavaCronetEngine.shutdown() is
      // called which calls ExecutorService.shutdown() which doesn't wait for the thread to
      // finish running tasks, and then teardown() calls GC looking for leaks. One possible
      // modification would be to expose the ExecutorService and then have tests call
      // awaitTermination() but this would complicate things, and adding a 1s sleep() to
      // allow the ExecutorService to terminate did not increase the chances of catching the
      // leak.
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @RequiresMinApi(8) // JavaUrlRequest fixed in API level 8: crrev.com/499303
  /** Do a HEAD request and get back a 404. */
  public void test404Head() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getFileURL("/notfound.html"), callback, callback.getExecutor());
    builder.setHttpMethod("HEAD").build().start();
    callback.blockForDone();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @RequiresMinApi(9) // Tagging support added in API level 9: crrev.com/c/chromium/src/+/930086
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1521")
  public void testTagging() throws Exception {}

  @Test
  @SmallTest
  @Feature({"Cronet"})
  /**
   * Initiate many requests concurrently to make sure neither Cronet implementation crashes.
   */
  public void testManyRequests() throws Exception {
    String url = NativeTestServer.getMultiRedirectURL();
    // Jelly Bean has a 2000 limit on global references, crbug.com/922656.
    final int numRequests = Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT ? 2000 : 1500;
    TestUrlRequestCallback callbacks[] = new TestUrlRequestCallback[numRequests];
    UrlRequest requests[] = new UrlRequest[numRequests];
    for (int i = 0; i < numRequests; i++) {
      // Share the first callback's executor to avoid creating too many single-threaded
      // executors and hence too many threads.
      if (i == 0) {
        callbacks[i] = new TestUrlRequestCallback();
      } else {
        callbacks[i] = new TestUrlRequestCallback(callbacks[0].getExecutor());
      }
      UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
          url, callbacks[i], callbacks[i].getExecutor());
      requests[i] = builder.build();
    }
    for (UrlRequest request : requests) {
      request.start();
    }
    for (UrlRequest request : requests) {
      request.cancel();
    }
    for (TestUrlRequestCallback callback : callbacks) {
      callback.blockForDone();
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testSetIdempotency() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    ExperimentalUrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoMethodURL(), callback, callback.getExecutor());
    assertEquals(builder.setIdempotency(ExperimentalUrlRequest.Builder.IDEMPOTENT), builder);

    TestUploadDataProvider dataProvider = new TestUploadDataProvider(
        TestUploadDataProvider.SuccessCallbackMode.SYNC, callback.getExecutor());
    dataProvider.addRead("test".getBytes());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("content-type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    dataProvider.assertClosed();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("POST", callback.mResponseAsString);
  }
}
