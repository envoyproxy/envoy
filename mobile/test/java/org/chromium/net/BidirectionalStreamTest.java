package org.chromium.net;

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import static org.chromium.net.testing.CronetTestRule.SERVER_CERT_PEM;
import static org.chromium.net.testing.CronetTestRule.SERVER_KEY_PKCS8_PEM;
import static org.chromium.net.testing.CronetTestRule.assertContains;
import static org.chromium.net.testing.CronetTestRule.getContext;

import android.os.ConditionVariable;
import android.os.Process;
import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;

import com.android.org.chromium.net.NetError;

import org.chromium.net.impl.BidirectionalStreamNetworkException;
import org.chromium.net.impl.CronetBidirectionalStream;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestUtil;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.Http2TestServer;
import org.chromium.net.testing.MetricsTestUtil;
import org.chromium.net.testing.TestBidirectionalStreamCallback;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.net.testing.CronetTestRule.OnlyRunNativeCronet;
import org.chromium.net.testing.CronetTestRule.RequiresMinApi;
import org.chromium.net.testing.MetricsTestUtil.TestRequestFinishedListener;
import org.chromium.net.testing.TestBidirectionalStreamCallback.FailureType;
import org.chromium.net.testing.TestBidirectionalStreamCallback.ResponseStep;
import org.chromium.net.impl.UrlResponseInfoImpl;

import java.nio.ByteBuffer;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Test functionality of BidirectionalStream interface.
 */
@RunWith(AndroidJUnit4.class)
public class BidirectionalStreamTest {

  private static final String TAG = BidirectionalStreamTest.class.getSimpleName();

  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private ExperimentalCronetEngine mCronetEngine;

  @Before
  public void setUp() throws Exception {
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    CronetTestUtil.getCronetEngineBuilderImpl(builder).setLogLevel("info");
    CronetTestUtil.setMockCertVerifierForTesting(builder);

    mCronetEngine = builder.build();
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

  private static void checkResponseInfo(UrlResponseInfo responseInfo, String expectedUrl,
                                        int expectedHttpStatusCode, String expectedHttpStatusText) {
    assertEquals(expectedUrl, responseInfo.getUrl());
    assertEquals(expectedUrl,
                 responseInfo.getUrlChain().get(responseInfo.getUrlChain().size() - 1));
    assertEquals(expectedHttpStatusCode, responseInfo.getHttpStatusCode());
    assertEquals(expectedHttpStatusText, responseInfo.getHttpStatusText());
    assertFalse(responseInfo.wasCached());
    assertTrue(responseInfo.toString().length() > 0);
  }

  private static String createLongString(String base, int repetition) {
    StringBuilder builder = new StringBuilder(base.length() * repetition);
    for (int i = 0; i < repetition; ++i) {
      builder.append(i);
      builder.append(base);
    }
    return builder.toString();
  }

  private static UrlResponseInfo createUrlResponseInfo(String[] urls, String message,
                                                       int statusCode, int receivedBytes,
                                                       String... headers) {
    ArrayList<Map.Entry<String, String>> headersList = new ArrayList<>();
    for (int i = 0; i < headers.length; i += 2) {
      headersList.add(
          new AbstractMap.SimpleImmutableEntry<String, String>(headers[i], headers[i + 1]));
    }
    UrlResponseInfoImpl urlResponseInfo = new UrlResponseInfoImpl(
        Arrays.asList(urls), statusCode, message, headersList, false, "h2", null, receivedBytes);
    return urlResponseInfo;
  }

  private void runSimpleGetWithExpectedReceivedByteCount(int expectedReceivedBytes)
      throws Exception {
    String url = Http2TestServer.getEchoMethodUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    TestRequestFinishedListener requestFinishedListener = new TestRequestFinishedListener();
    mCronetEngine.addRequestFinishedListener(requestFinishedListener);
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .setHttpMethod("GET")
            .build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    requestFinishedListener.blockUntilDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    // Default method is 'GET'.
    assertEquals("GET", callback.mResponseAsString);
    UrlResponseInfo urlResponseInfo =
        createUrlResponseInfo(new String[] {url}, "", 200, expectedReceivedBytes, ":status", "200");
    mTestRule.assertResponseEquals(urlResponseInfo, callback.mResponseInfo);
    checkResponseInfo(callback.mResponseInfo, Http2TestServer.getEchoMethodUrl(), 200, "");
    RequestFinishedInfo finishedInfo = requestFinishedListener.getRequestInfo();
    assertTrue(finishedInfo.getAnnotations().isEmpty());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBuilderCheck() throws Exception {
    if (mTestRule.testingJavaImpl()) {
      runBuilderCheckJavaImpl();
    } else {
      runBuilderCheckNativeImpl();
    }
  }

  private void runBuilderCheckNativeImpl() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    try {
      mCronetEngine.newBidirectionalStreamBuilder(null, callback, callback.getExecutor());
      fail("URL not null-checked");
    } catch (NullPointerException e) {
      assertEquals("URL is required.", e.getMessage());
    }
    try {
      mCronetEngine.newBidirectionalStreamBuilder(Http2TestServer.getServerUrl(), null,
                                                  callback.getExecutor());
      fail("Callback not null-checked");
    } catch (NullPointerException e) {
      assertEquals("Callback is required.", e.getMessage());
    }
    try {
      mCronetEngine.newBidirectionalStreamBuilder(Http2TestServer.getServerUrl(), callback, null);
      fail("Executor not null-checked");
    } catch (NullPointerException e) {
      assertEquals("Executor is required.", e.getMessage());
    }
    // Verify successful creation doesn't throw.
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getServerUrl(), callback, callback.getExecutor());
    try {
      builder.addHeader(null, "value");
      fail("Header name is not null-checked");
    } catch (NullPointerException e) {
      assertEquals("Invalid header name.", e.getMessage());
    }
    try {
      builder.addHeader("name", null);
      fail("Header value is not null-checked");
    } catch (NullPointerException e) {
      assertEquals("Invalid header value.", e.getMessage());
    }
    try {
      builder.setHttpMethod(null);
      fail("Method name is not null-checked");
    } catch (NullPointerException e) {
      assertEquals("Method is required.", e.getMessage());
    }
  }

  private void runBuilderCheckJavaImpl() {
    try {
      TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
      mTestRule.createJavaEngineBuilder().build().newBidirectionalStreamBuilder(
          Http2TestServer.getServerUrl(), callback, callback.getExecutor());
      fail("JavaCronetEngine doesn't support BidirectionalStream."
           + " Expected UnsupportedOperationException");
    } catch (UnsupportedOperationException e) {
      // Expected.
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testFailPlainHttp() throws Exception {
    String url = "http://example.com";
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor()).build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertContains("Exception in BidirectionalStream: net::ERR_DISALLOWED_URL_SCHEME",
                   callback.mError.getMessage());
    assertEquals(-301, ((NetworkException)callback.mError).getCronetInternalErrorCode());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("fix expected ReceivedByteCount - quite unpredictable")
  public void testSimpleGet() throws Exception {
    // Since this is the first request on the connection, the expected received bytes count
    // must account for an HPACK dynamic table size update.
    runSimpleGetWithExpectedReceivedByteCount(27);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("To be investigated - head does not work")
  public void testSimpleHead() throws Exception {
    String url = Http2TestServer.getEchoMethodUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .setHttpMethod("HEAD")
            .build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("HEAD", callback.mResponseAsString);
    UrlResponseInfo urlResponseInfo =
        createUrlResponseInfo(new String[] {url}, "", 200, 32, ":status", "200");
    mTestRule.assertResponseEquals(urlResponseInfo, callback.mResponseInfo);
    checkResponseInfo(callback.mResponseInfo, Http2TestServer.getEchoMethodUrl(), 200, "");
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimplePost() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.addWriteData("Test String".getBytes());
    callback.addWriteData("1234567890".getBytes());
    callback.addWriteData("woot!".getBytes());
    TestRequestFinishedListener requestFinishedListener = new TestRequestFinishedListener();
    mCronetEngine.addRequestFinishedListener(requestFinishedListener);
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .addHeader("foo", "bar")
            .addHeader("empty", "")
            .addHeader("Content-Type", "zebra")
            .addRequestAnnotation(this)
            .addRequestAnnotation("request annotation")
            .build();
    Date startTime = new Date();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    requestFinishedListener.blockUntilDone();
    Date endTime = new Date();
    RequestFinishedInfo finishedInfo = requestFinishedListener.getRequestInfo();
    MetricsTestUtil.checkRequestFinishedInfo(finishedInfo, url, startTime, endTime);
    assertEquals(RequestFinishedInfo.SUCCEEDED, finishedInfo.getFinishedReason());
    MetricsTestUtil.checkHasConnectTiming(finishedInfo.getMetrics(), startTime, endTime, true);
    assertThat(finishedInfo.getAnnotations()).containsExactly("request annotation", this);
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Test String1234567890woot!", callback.mResponseAsString);
    assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
    assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
    assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimpleGetWithCombinedHeader() throws Exception {
    String url = Http2TestServer.getCombinedHeadersUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    TestRequestFinishedListener requestFinishedListener = new TestRequestFinishedListener();
    mCronetEngine.addRequestFinishedListener(requestFinishedListener);
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .setHttpMethod("GET")
            .build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    requestFinishedListener.blockUntilDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    // Default method is 'GET'.
    assertEquals("GET", callback.mResponseAsString);
    assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("foo").get(0));
    assertEquals("bar2", callback.mResponseInfo.getAllHeaders().get("foo").get(1));
    RequestFinishedInfo finishedInfo = requestFinishedListener.getRequestInfo();
    assertTrue(finishedInfo.getAnnotations().isEmpty());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimplePostWithFlush() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.addWriteData("Test String".getBytes(), false);
    callback.addWriteData("1234567890".getBytes(), false);
    callback.addWriteData("woot!".getBytes(), true);
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .addHeader("foo", "bar")
            .addHeader("empty", "")
            .addHeader("Content-Type", "zebra")
            .build();
    // Flush before stream is started should not crash.
    stream.flush();

    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());

    // Flush after stream is completed is no-op. It shouldn't call into the destroyed adapter.
    stream.flush();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Test String1234567890woot!", callback.mResponseAsString);
    assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
    assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
    assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  // Tests that a delayed flush() only sends buffers that have been written
  // before it is called, and it doesn't flush buffers in mPendingQueue.
  public void testFlushData() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    final ConditionVariable waitOnStreamReady = new ConditionVariable();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback() {
      // Number of onWriteCompleted callbacks that have been invoked.
      private int mNumWriteCompleted;

      @Override
      public void onStreamReady(BidirectionalStream stream) {
        mResponseStep = ResponseStep.ON_STREAM_READY;
        waitOnStreamReady.open();
      }

      @Override
      public void onWriteCompleted(BidirectionalStream stream, UrlResponseInfo info,
                                   ByteBuffer buffer, boolean endOfStream) {
        super.onWriteCompleted(stream, info, buffer, endOfStream);
        mNumWriteCompleted++;
        if (mNumWriteCompleted <= 3) {
          // "6" is in pending queue.
          List<ByteBuffer> pendingData =
              ((CronetBidirectionalStream)stream).getPendingDataForTesting();
          assertEquals(1, pendingData.size());
          ByteBuffer pendingBuffer = pendingData.get(0);
          byte[] content = new byte[pendingBuffer.remaining()];
          pendingBuffer.get(content);
          assertTrue(Arrays.equals("6".getBytes(), content));

          // "4" and "5" have been flushed.
          assertEquals(0, ((CronetBidirectionalStream)stream).getFlushDataForTesting().size());
        } else if (mNumWriteCompleted == 5) {
          // Now flush "6", which is still in pending queue.
          List<ByteBuffer> pendingData =
              ((CronetBidirectionalStream)stream).getPendingDataForTesting();
          assertEquals(1, pendingData.size());
          ByteBuffer pendingBuffer = pendingData.get(0);
          byte[] content = new byte[pendingBuffer.remaining()];
          pendingBuffer.get(content);
          assertTrue(Arrays.equals("6".getBytes(), content));

          stream.flush();

          assertEquals(0, ((CronetBidirectionalStream)stream).getPendingDataForTesting().size());
          assertEquals(0, ((CronetBidirectionalStream)stream).getFlushDataForTesting().size());
        }
      }
    };
    callback.addWriteData("1".getBytes(), false);
    callback.addWriteData("2".getBytes(), false);
    callback.addWriteData("3".getBytes(), true);
    callback.addWriteData("4".getBytes(), false);
    callback.addWriteData("5".getBytes(), true);
    callback.addWriteData("6".getBytes(), false);
    CronetBidirectionalStream stream =
        (CronetBidirectionalStream)mCronetEngine
            .newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .addHeader("foo", "bar")
            .addHeader("empty", "")
            .addHeader("Content-Type", "zebra")
            .build();
    stream.start();
    waitOnStreamReady.block();

    assertEquals(0, stream.getPendingDataForTesting().size());
    assertEquals(0, stream.getFlushDataForTesting().size());

    // Write 1, 2, 3 and flush() - subsequent flush are performed by the onWriteCompleted callback.
    callback.startNextWrite(stream);

    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("123456", callback.mResponseAsString);
    assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
    assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
    assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/2213")
  // Regression test for crbug.com/692168.
  public void testCancelWhileWriteDataPending() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    // Use a direct executor to avoid race.
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback(
        /*useDirectExecutor*/ false) {
      @Override
      public void onStreamReady(BidirectionalStream stream) {
        // Start the first write.
        stream.write(getDummyData(), false);
        stream.flush();
      }

      @Override
      public void onReadCompleted(BidirectionalStream stream, UrlResponseInfo info,
                                  ByteBuffer byteBuffer, boolean endOfStream) {
        super.onReadCompleted(stream, info, byteBuffer, endOfStream);
        // Cancel now when the write side is busy.
        stream.cancel();
      }

      @Override
      public void onWriteCompleted(BidirectionalStream stream, UrlResponseInfo info,
                                   ByteBuffer buffer, boolean endOfStream) {
        // Flush twice to keep the flush queue non-empty.
        stream.write(getDummyData(), false);
        stream.flush();
        stream.write(getDummyData(), false);
        stream.flush();
      }

      // Returns a piece of dummy data to send to the server.
      private ByteBuffer getDummyData() {
        byte[] data = new byte[100];
        for (int i = 0; i < data.length; i++) {
          data[i] = 'x';
        }
        ByteBuffer dummyData = ByteBuffer.allocateDirect(data.length);
        dummyData.put(data);
        dummyData.flip();
        return dummyData;
      }
    };
    CronetBidirectionalStream stream =
        (CronetBidirectionalStream)mCronetEngine
            .newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .build();
    stream.start();
    callback.blockForDone();
    assertTrue(callback.mOnCanceledCalled);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimpleGetWithFlush() throws Exception {
    // TODO(xunjieli): Use ParameterizedTest instead of the loop.
    for (int i = 0; i < 2; i++) {
      String url = Http2TestServer.getEchoStreamUrl();
      TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback() {
        @Override
        public void onStreamReady(BidirectionalStream stream) {
          try {
            // Attempt to write data for GET request.
            stream.write(ByteBuffer.wrap("dummy".getBytes()), true);
          } catch (IllegalArgumentException e) {
            // Expected.
          }
          // If there are delayed headers, this flush should try to send them.
          // If nothing to flush, it should not crash.
          stream.flush();
          super.onStreamReady(stream);
          try {
            // Attempt to write data for GET request.
            stream.write(ByteBuffer.wrap("dummy".getBytes()), true);
          } catch (IllegalArgumentException e) {
            // Expected.
          }
        }
      };
      BidirectionalStream stream =
          mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
              .setHttpMethod("GET")
              .delayRequestHeadersUntilFirstFlush(i == 0)
              .addHeader("foo", "bar")
              .addHeader("empty", "")
              .build();
      // Flush before stream is started should not crash.
      stream.flush();

      stream.start();
      callback.blockForDone();
      assertTrue(stream.isDone());

      // Flush after stream is completed is no-op. It shouldn't call into the destroyed
      // adapter.
      stream.flush();

      assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
      assertEquals("", callback.mResponseAsString);
      assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
      assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimplePostWithFlushAfterOneWrite() throws Exception {
    // TODO(xunjieli): Use ParameterizedTest instead of the loop.
    for (int i = 0; i < 2; i++) {
      String url = Http2TestServer.getEchoStreamUrl();
      TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
      callback.addWriteData("Test String".getBytes(), true);
      BidirectionalStream stream =
          mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
              .delayRequestHeadersUntilFirstFlush(i == 0)
              .addHeader("foo", "bar")
              .addHeader("empty", "")
              .addHeader("Content-Type", "zebra")
              .build();
      stream.start();
      callback.blockForDone();
      assertTrue(stream.isDone());

      assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
      assertEquals("Test String", callback.mResponseAsString);
      assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
      assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
      assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimplePostWithFlushTwice() throws Exception {
    // TODO(xunjieli): Use ParameterizedTest instead of the loop.
    for (int i = 0; i < 2; i++) {
      String url = Http2TestServer.getEchoStreamUrl();
      TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
      callback.addWriteData("Test String".getBytes(), false);
      callback.addWriteData("1234567890".getBytes(), false);
      callback.addWriteData("woot!".getBytes(), true);
      callback.addWriteData("Test String".getBytes(), false);
      callback.addWriteData("1234567890".getBytes(), false);
      callback.addWriteData("woot!".getBytes(), true);
      BidirectionalStream stream =
          mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
              .delayRequestHeadersUntilFirstFlush(i == 0)
              .addHeader("foo", "bar")
              .addHeader("empty", "")
              .addHeader("Content-Type", "zebra")
              .build();
      stream.start();
      callback.blockForDone();
      assertTrue(stream.isDone());
      assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
      assertEquals("Test String1234567890woot!Test String1234567890woot!",
                   callback.mResponseAsString);
      assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
      assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
      assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  // Tests that it is legal to call read() in onStreamReady().
  public void testReadDuringOnStreamReady() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback() {
      @Override
      public void onStreamReady(BidirectionalStream stream) {
        super.onStreamReady(stream);
        startNextRead(stream);
      }

      @Override
      public void onResponseHeadersReceived(BidirectionalStream stream, UrlResponseInfo info) {
        // Do nothing. Skip readng.
      }
    };
    callback.addWriteData("Test String".getBytes());
    callback.addWriteData("1234567890".getBytes());
    callback.addWriteData("woot!".getBytes());
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .addHeader("foo", "bar")
            .addHeader("empty", "")
            .addHeader("Content-Type", "zebra")
            .build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Test String1234567890woot!", callback.mResponseAsString);
    assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
    assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
    assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  // Tests that it is legal to call flush() when previous nativeWritevData has
  // yet to complete.
  public void testSimplePostWithFlushBeforePreviousWriteCompleted() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback() {
      @Override
      public void onStreamReady(BidirectionalStream stream) {
        super.onStreamReady(stream);
        // Write a second time before the previous nativeWritevData has completed.
        startNextWrite(stream);
        assertEquals(0, numPendingWrites());
      }
    };
    callback.addWriteData("Test String".getBytes(), false);
    callback.addWriteData("1234567890".getBytes(), false);
    callback.addWriteData("woot!".getBytes(), true);
    callback.addWriteData("Test String".getBytes(), false);
    callback.addWriteData("1234567890".getBytes(), false);
    callback.addWriteData("woot!".getBytes(), true);
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .addHeader("foo", "bar")
            .addHeader("empty", "")
            .addHeader("Content-Type", "zebra")
            .build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Test String1234567890woot!Test String1234567890woot!",
                 callback.mResponseAsString);
    assertEquals("bar", callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
    assertEquals("", callback.mResponseInfo.getAllHeaders().get("echo-empty").get(0));
    assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimplePut() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.addWriteData("Put This Data!".getBytes());
    String methodName = "PUT";
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getServerUrl(), callback, callback.getExecutor());
    builder.setHttpMethod(methodName);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("Put This Data!", callback.mResponseAsString);
    assertEquals(methodName, callback.mResponseInfo.getAllHeaders().get("echo-method").get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testBadMethod() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getServerUrl(), callback, callback.getExecutor());
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
  @OnlyRunNativeCronet
  public void testBadHeaderName() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getServerUrl(), callback, callback.getExecutor());
    try {
      builder.addHeader("goodheader1", "headervalue");
      builder.addHeader("header:name", "headervalue");
      builder.addHeader("goodheader2", "headervalue");
      builder.build().start();
      fail("IllegalArgumentException not thrown.");
    } catch (IllegalArgumentException e) {
      assertEquals("Invalid header header:name=headervalue", e.getMessage());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testBadHeaderValue() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getServerUrl(), callback, callback.getExecutor());
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
  @OnlyRunNativeCronet
  public void testAddHeader() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    String headerName = "header-name";
    String headerValue = "header-value";
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoHeaderUrl(headerName), callback, callback.getExecutor());
    builder.addHeader(headerName, headerValue);
    builder.setHttpMethod("GET");
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(headerValue, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Cronet does not support multi-headers - EM does")
  public void testMultiRequestHeaders() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    String headerName = "header-name";
    String headerValue1 = "header-value1";
    String headerValue2 = "header-value2";
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoAllHeadersUrl(), callback, callback.getExecutor());
    builder.addHeader(headerName, headerValue1);
    builder.addHeader(headerName, headerValue2);
    builder.setHttpMethod("GET");
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
  @OnlyRunNativeCronet
  @Ignore("okhttp returns :status as a header")
  public void testEchoTrailers() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    String headerName = "header-name";
    String headerValue = "header-value";
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoTrailersUrl(), callback, callback.getExecutor());
    builder.addHeader(headerName, headerValue);
    builder.setHttpMethod("GET");
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertNotNull(callback.mTrailers);
    // Verify that header value is properly echoed in trailers.
    assertEquals(headerValue, callback.mTrailers.getAsMap().get("echo-" + headerName).get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testCustomUserAgent() throws Exception {
    String userAgentName = "User-Agent";
    String userAgentValue = "User-Agent-Value";
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoHeaderUrl(userAgentName), callback, callback.getExecutor());
    builder.setHttpMethod("GET");
    builder.addHeader(userAgentName, userAgentValue);
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(userAgentValue, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testCustomCronetEngineUserAgent() throws Exception {
    String userAgentName = "User-Agent";
    String userAgentValue = "User-Agent-Value";
    ExperimentalCronetEngine.Builder engineBuilder =
        new ExperimentalCronetEngine.Builder(getContext());
    engineBuilder.setUserAgent(userAgentValue);
    CronetTestUtil.setMockCertVerifierForTesting(engineBuilder);
    ExperimentalCronetEngine engine = engineBuilder.build();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = engine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoHeaderUrl(userAgentName), callback, callback.getExecutor());
    builder.setHttpMethod("GET");
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(userAgentValue, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testDefaultUserAgent() throws Exception {
    String userAgentName = "User-Agent";
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoHeaderUrl(userAgentName), callback, callback.getExecutor());
    builder.setHttpMethod("GET");
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(new CronetEngine.Builder(getContext()).getDefaultUserAgent(),
                 callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testEchoStream() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    String[] testData = {"Test String", createLongString("1234567890", 50000), "woot!"};
    StringBuilder stringData = new StringBuilder();
    for (String writeData : testData) {
      callback.addWriteData(writeData.getBytes());
      stringData.append(writeData);
    }
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
            .addHeader("foo", "Value with Spaces")
            .addHeader("Content-Type", "zebra")
            .build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(stringData.toString(), callback.mResponseAsString);
    assertEquals("Value with Spaces",
                 callback.mResponseInfo.getAllHeaders().get("echo-foo").get(0));
    assertEquals("zebra", callback.mResponseInfo.getAllHeaders().get("echo-content-type").get(0));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testEchoStreamEmptyWrite() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.addWriteData(new byte[0]);
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor()).build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testDoubleWrite() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback() {
      @Override
      public void onStreamReady(BidirectionalStream stream) {
        // super class will call Write() once.
        super.onStreamReady(stream);
        // Call Write() again.
        startNextWrite(stream);
        // Make sure there is no pending write.
        assertEquals(0, numPendingWrites());
      }
    };
    callback.addWriteData("1".getBytes());
    callback.addWriteData("2".getBytes());
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor()).build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("12", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testDoubleRead() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback() {
      @Override
      public void onResponseHeadersReceived(BidirectionalStream stream, UrlResponseInfo info) {
        startNextRead(stream);
        try {
          // Second read from callback invoked on single-threaded executor throws
          // an exception because previous read is still pending until its completion
          // is handled on executor.
          stream.read(ByteBuffer.allocateDirect(5));
          fail("Exception is not thrown.");
        } catch (Exception e) {
          assertEquals("Unexpected read attempt.", e.getMessage());
        }
      }
    };
    callback.addWriteData("1".getBytes());
    callback.addWriteData("2".getBytes());
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor()).build();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("12", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Disabled due to timeout. See crbug.com/591112")
  public void testReadAndWrite() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback() {
      @Override
      public void onResponseHeadersReceived(BidirectionalStream stream, UrlResponseInfo info) {
        // Start the write, that will not complete until callback completion.
        startNextWrite(stream);
        // Start the read. It is allowed with write in flight.
        super.onResponseHeadersReceived(stream, info);
      }
    };
    callback.setAutoAdvance(false);
    callback.addWriteData("1".getBytes());
    callback.addWriteData("2".getBytes());
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor()).build();
    stream.start();
    callback.waitForNextWriteStep();
    callback.waitForNextReadStep();
    callback.startNextRead(stream);
    callback.setAutoAdvance(true);
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("12", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testEchoStreamWriteFirst() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.setAutoAdvance(false);
    String[] testData = {"a", "bb", "ccc", "Test String", "1234567890", "woot!"};
    StringBuilder stringData = new StringBuilder();
    for (String writeData : testData) {
      callback.addWriteData(writeData.getBytes());
      stringData.append(writeData);
    }
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor()).build();
    stream.start();
    // Write first.
    callback.waitForNextWriteStep(); // onStreamReady
    for (String expected : testData) {
      // Write next chunk of test data.
      callback.startNextWrite(stream);
      callback.waitForNextWriteStep(); // onWriteCompleted
    }

    // Wait for read step, but don't read yet.
    callback.waitForNextReadStep(); // onResponseHeadersReceived
    assertEquals("", callback.mResponseAsString);
    // Read back.
    callback.startNextRead(stream);
    callback.waitForNextReadStep(); // onReadCompleted
    // Verify that some part of proper response is read.
    assertTrue(callback.mResponseAsString.startsWith(testData[0]));
    assertTrue(stringData.toString().startsWith(callback.mResponseAsString));
    // Read the rest of the response.
    callback.setAutoAdvance(true);
    callback.startNextRead(stream);
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(stringData.toString(), callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testEchoStreamStepByStep() throws Exception {
    String url = Http2TestServer.getEchoStreamUrl();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.setAutoAdvance(false);
    String[] testData = {"a", "bb", "ccc", "Test String", "1234567890", "woot!"};
    StringBuilder stringData = new StringBuilder();
    for (String writeData : testData) {
      callback.addWriteData(writeData.getBytes());
      stringData.append(writeData);
    }
    // Create stream.
    BidirectionalStream stream =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor()).build();
    stream.start();
    callback.waitForNextWriteStep();
    callback.waitForNextReadStep();

    for (String expected : testData) {
      // Write next chunk of test data.
      callback.startNextWrite(stream);
      callback.waitForNextWriteStep();

      // Read next chunk of test data.
      ByteBuffer readBuffer = ByteBuffer.allocateDirect(100);
      callback.startNextRead(stream, readBuffer);
      callback.waitForNextReadStep();
      assertEquals(expected.length(), readBuffer.position());
      assertFalse(stream.isDone());
    }

    callback.setAutoAdvance(true);
    callback.startNextRead(stream);
    callback.blockForDone();
    assertTrue(stream.isDone());
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(stringData.toString(), callback.mResponseAsString);
  }

  /**
   * Checks that the buffer is updated correctly, when starting at an offset.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testSimpleGetBufferUpdates() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.setAutoAdvance(false);
    // Since the method is "GET", the expected response body is also "GET".
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    BidirectionalStream stream = builder.setHttpMethod("GET").build();
    stream.start();
    callback.waitForNextReadStep();

    assertEquals(null, callback.mError);
    assertFalse(callback.isDone());
    assertEquals(TestBidirectionalStreamCallback.ResponseStep.ON_RESPONSE_STARTED,
                 callback.mResponseStep);

    ByteBuffer readBuffer = ByteBuffer.allocateDirect(5);
    readBuffer.put("FOR".getBytes());
    assertEquals(3, readBuffer.position());

    // Read first two characters of the response ("GE"). It's theoretically
    // possible to need one read per character, though in practice,
    // shouldn't happen.
    while (callback.mResponseAsString.length() < 2) {
      assertFalse(callback.isDone());
      callback.startNextRead(stream, readBuffer);
      callback.waitForNextReadStep();
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
    callback.startNextRead(stream, readBuffer);
    callback.waitForNextReadStep();

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
    callback.setAutoAdvance(true);
    callback.startNextRead(stream, readBuffer);
    callback.blockForDone();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("GET", callback.mResponseAsString);
    checkResponseInfo(callback.mResponseInfo, Http2TestServer.getEchoMethodUrl(), 200, "");

    // Check that buffer contents were not modified.
    assertEquals("FORTE", bufferContentsToString(readBuffer, 0, 5));

    // Position should not have been modified, since nothing was read.
    assertEquals(1, readBuffer.position());
    // Limit should be unchanged as always.
    assertEquals(5, readBuffer.limit());

    assertEquals(ResponseStep.ON_SUCCEEDED, callback.mResponseStep);

    // Make sure there are no other pending messages, which would trigger
    // asserts in TestBidirectionalCallback.
    // The expected received bytes count is lower than it would be for the first request on the
    // connection, because the server includes an HPACK dynamic table size update only in the
    // first response HEADERS frame.
    // TODO: fix expected ReceivedByteCount - quite unpredictable
    // runSimpleGetWithExpectedReceivedByteCount(27);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testBadBuffers() throws Exception {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.setAutoAdvance(false);
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    BidirectionalStream stream = builder.setHttpMethod("GET").build();
    stream.start();
    callback.waitForNextReadStep();

    assertEquals(null, callback.mError);
    assertFalse(callback.isDone());
    assertEquals(TestBidirectionalStreamCallback.ResponseStep.ON_RESPONSE_STARTED,
                 callback.mResponseStep);

    // Try to read using a full buffer.
    try {
      ByteBuffer readBuffer = ByteBuffer.allocateDirect(4);
      readBuffer.put("full".getBytes());
      stream.read(readBuffer);
      fail("Exception not thrown");
    } catch (IllegalArgumentException e) {
      assertEquals("ByteBuffer is already full.", e.getMessage());
    }

    // Try to read using a non-direct buffer.
    try {
      ByteBuffer readBuffer = ByteBuffer.allocate(5);
      stream.read(readBuffer);
      fail("Exception not thrown");
    } catch (Exception e) {
      assertEquals("byteBuffer must be a direct ByteBuffer.", e.getMessage());
    }

    // Finish the stream with a direct ByteBuffer.
    callback.setAutoAdvance(true);
    ByteBuffer readBuffer = ByteBuffer.allocateDirect(5);
    stream.read(readBuffer);
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals("GET", callback.mResponseAsString);
  }

  private void throwOrCancel(FailureType failureType, ResponseStep failureStep,
                             boolean expectError) {
    // Use a fresh CronetEngine each time so Http2 session is not reused.
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    CronetTestUtil.setMockCertVerifierForTesting(builder);
    mCronetEngine = builder.build();
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.setFailure(failureType, failureStep);
    TestRequestFinishedListener requestFinishedListener = new TestRequestFinishedListener();
    mCronetEngine.addRequestFinishedListener(requestFinishedListener);
    BidirectionalStream.Builder streamBuilder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    BidirectionalStream stream = streamBuilder.setHttpMethod("GET").build();
    Date startTime = new Date();
    stream.start();
    callback.blockForDone();
    assertTrue(stream.isDone());
    requestFinishedListener.blockUntilDone();
    Date endTime = new Date();
    RequestFinishedInfo finishedInfo = requestFinishedListener.getRequestInfo();
    RequestFinishedInfo.Metrics metrics = finishedInfo.getMetrics();
    assertNotNull(metrics);
    // Cancellation when stream is ready does not guarantee that
    // mResponseInfo is null because there might be a
    // onResponseHeadersReceived already queued in the executor.
    // See crbug.com/594432.
    if (failureStep != ResponseStep.ON_STREAM_READY) {
      assertNotNull(callback.mResponseInfo);
    }
    // Check metrics information.
    if (failureStep == ResponseStep.ON_RESPONSE_STARTED ||
        failureStep == ResponseStep.ON_READ_COMPLETED || failureStep == ResponseStep.ON_TRAILERS) {
      // For steps after response headers are received, there will be
      // connect timing metrics.
      // TODO(https://github.com/envoyproxy/envoy-mobile/issues/2192) uncomment this line
      // MetricsTestUtil.checkTimingMetrics(metrics, startTime, endTime);
      MetricsTestUtil.checkHasConnectTiming(metrics, startTime, endTime, true);
      assertTrue(metrics.getSentByteCount() > 0);
      assertTrue(metrics.getReceivedByteCount() > 0);
    } else if (failureStep == ResponseStep.ON_STREAM_READY) {
      assertNotNull(metrics.getRequestStart());
      MetricsTestUtil.assertAfter(metrics.getRequestStart(), startTime);
      assertNotNull(metrics.getRequestEnd());
      MetricsTestUtil.assertAfter(endTime, metrics.getRequestEnd());
      MetricsTestUtil.assertAfter(metrics.getRequestEnd(), metrics.getRequestStart());
    }
    assertEquals(expectError, callback.mError != null);
    assertEquals(expectError, callback.mOnErrorCalled);
    if (expectError) {
      assertNotNull(finishedInfo.getException());
      assertEquals(RequestFinishedInfo.FAILED, finishedInfo.getFinishedReason());
    } else {
      assertNull(finishedInfo.getException());
      assertEquals(RequestFinishedInfo.CANCELED, finishedInfo.getFinishedReason());
    }
    assertEquals(failureType == FailureType.CANCEL_SYNC ||
                     failureType == FailureType.CANCEL_ASYNC ||
                     failureType == FailureType.CANCEL_ASYNC_WITHOUT_PAUSE,
                 callback.mOnCanceledCalled);
    mCronetEngine.removeRequestFinishedListener(requestFinishedListener);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Flaky: crashes EM")
  public void testFailures() throws Exception {
    // TODO: start time and end time are not set.
    //  throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_STREAM_READY, false);
    //  throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_STREAM_READY, false);
    //  throwOrCancel(FailureType.CANCEL_ASYNC_WITHOUT_PAUSE, ResponseStep.ON_STREAM_READY, false);
    //  throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_STREAM_READY, true);

    throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_RESPONSE_STARTED, false);
    throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_RESPONSE_STARTED, false);
    throwOrCancel(FailureType.CANCEL_ASYNC_WITHOUT_PAUSE, ResponseStep.ON_RESPONSE_STARTED, false);
    throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_RESPONSE_STARTED, true);

    throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_READ_COMPLETED, false);
    throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_READ_COMPLETED, false);
    throwOrCancel(FailureType.CANCEL_ASYNC_WITHOUT_PAUSE, ResponseStep.ON_READ_COMPLETED, false);
    throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_READ_COMPLETED, true);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testThrowOnSucceeded() {
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.setFailure(FailureType.THROW_SYNC, ResponseStep.ON_SUCCEEDED);
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    BidirectionalStream stream = builder.setHttpMethod("GET").build();
    stream.start();
    callback.blockForDone();
    assertEquals(callback.mResponseStep, ResponseStep.ON_SUCCEEDED);
    assertTrue(stream.isDone());
    assertNotNull(callback.mResponseInfo);
    // Check that error thrown from 'onSucceeded' callback is not reported.
    assertNull(callback.mError);
    assertFalse(callback.mOnErrorCalled);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testExecutorShutdownBeforeStreamIsDone() {
    // Test that stream is destroyed even if executor is shut down and rejects posting tasks.
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.setAutoAdvance(false);
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    CronetBidirectionalStream stream =
        (CronetBidirectionalStream)builder.setHttpMethod("GET").build();
    stream.start();
    callback.waitForNextReadStep();
    assertFalse(callback.isDone());
    assertFalse(stream.isDone());

    final ConditionVariable streamDestroyed = new ConditionVariable(false);
    stream.setOnDestroyedCallbackForTesting(new Runnable() {
      @Override
      public void run() {
        streamDestroyed.open();
      }
    });

    // Shut down the executor, so posting the task will throw an exception.
    callback.shutdownExecutor();
    ByteBuffer readBuffer = ByteBuffer.allocateDirect(5);
    stream.read(readBuffer);
    // Callback will never be called again because executor is shut down,
    // but stream will be destroyed from network thread.
    streamDestroyed.block();

    assertFalse(callback.isDone());
    assertTrue(stream.isDone());
  }

  /**
   * Callback that shuts down the engine when the stream has succeeded
   * or failed.
   */
  private class ShutdownTestBidirectionalStreamCallback extends TestBidirectionalStreamCallback {
    @Override
    public void onSucceeded(BidirectionalStream stream, UrlResponseInfo info) {
      mCronetEngine.shutdown();
      // Clear mCronetEngine so it doesn't get shut down second time in tearDown().
      mCronetEngine = null;
      super.onSucceeded(stream, info);
    }

    @Override
    public void onFailed(BidirectionalStream stream, UrlResponseInfo info, CronetException error) {
      mCronetEngine.shutdown();
      // Clear mCronetEngine so it doesn't get shut down second time in tearDown().
      mCronetEngine = null;
      super.onFailed(stream, info, error);
    }

    @Override
    public void onCanceled(BidirectionalStream stream, UrlResponseInfo info) {
      mCronetEngine.shutdown();
      // Clear mCronetEngine so it doesn't get shut down second time in tearDown().
      mCronetEngine = null;
      super.onCanceled(stream, info);
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testCronetEngineShutdown() throws Exception {
    // Test that CronetEngine cannot be shut down if there are any active streams.
    TestBidirectionalStreamCallback callback = new ShutdownTestBidirectionalStreamCallback();
    // Block callback when response starts to verify that shutdown fails
    // if there are active streams.
    callback.setAutoAdvance(false);
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    CronetBidirectionalStream stream =
        (CronetBidirectionalStream)builder.setHttpMethod("GET").build();
    stream.start();
    try {
      mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }

    callback.waitForNextReadStep();
    assertEquals(ResponseStep.ON_RESPONSE_STARTED, callback.mResponseStep);
    try {
      mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }
    callback.startNextRead(stream);

    callback.waitForNextReadStep();
    assertEquals(ResponseStep.ON_READ_COMPLETED, callback.mResponseStep);
    try {
      mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }

    // May not have read all the data, in theory. Just enable auto-advance
    // and finish the request.
    callback.setAutoAdvance(true);
    callback.startNextRead(stream);
    callback.blockForDone();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testCronetEngineShutdownAfterStreamFailure() throws Exception {
    // Test that CronetEngine can be shut down after stream reports a failure.
    TestBidirectionalStreamCallback callback = new ShutdownTestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    CronetBidirectionalStream stream =
        (CronetBidirectionalStream)builder.setHttpMethod("GET").build();
    stream.start();
    callback.setFailure(FailureType.THROW_SYNC, ResponseStep.ON_READ_COMPLETED);
    callback.blockForDone();
    assertTrue(callback.mOnErrorCalled);
    assertNull(mCronetEngine);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testCronetEngineShutdownAfterStreamCancel() throws Exception {
    // Test that CronetEngine can be shut down after stream is canceled.
    TestBidirectionalStreamCallback callback = new ShutdownTestBidirectionalStreamCallback();
    BidirectionalStream.Builder builder = mCronetEngine.newBidirectionalStreamBuilder(
        Http2TestServer.getEchoMethodUrl(), callback, callback.getExecutor());
    CronetBidirectionalStream stream =
        (CronetBidirectionalStream)builder.setHttpMethod("GET").build();

    // Block callback when response starts to verify that shutdown fails
    // if there are active requests.
    callback.setAutoAdvance(false);
    stream.start();
    try {
      mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }
    callback.waitForNextReadStep();
    assertEquals(ResponseStep.ON_RESPONSE_STARTED, callback.mResponseStep);
    stream.cancel();
    callback.blockForDone();
    assertTrue(callback.mOnCanceledCalled);
    assertNull(mCronetEngine);
  }

  /*
   * Verifies NetworkException constructed from specific error codes are retryable.
   */
  @SmallTest
  @Feature({"Cronet"})
  @Test
  @OnlyRunNativeCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1550")
  public void testErrorCodes() throws Exception {
    // Non-BidirectionalStream specific error codes.
    checkSpecificErrorCode(NetError.ERR_NAME_NOT_RESOLVED,
                           NetworkException.ERROR_HOSTNAME_NOT_RESOLVED, false);
    checkSpecificErrorCode(NetError.ERR_INTERNET_DISCONNECTED,
                           NetworkException.ERROR_INTERNET_DISCONNECTED, false);
    checkSpecificErrorCode(NetError.ERR_NETWORK_CHANGED, NetworkException.ERROR_NETWORK_CHANGED,
                           true);
    checkSpecificErrorCode(NetError.ERR_CONNECTION_CLOSED, NetworkException.ERROR_CONNECTION_CLOSED,
                           true);
    checkSpecificErrorCode(NetError.ERR_CONNECTION_REFUSED,
                           NetworkException.ERROR_CONNECTION_REFUSED, false);
    checkSpecificErrorCode(NetError.ERR_CONNECTION_RESET, NetworkException.ERROR_CONNECTION_RESET,
                           true);
    checkSpecificErrorCode(NetError.ERR_CONNECTION_TIMED_OUT,
                           NetworkException.ERROR_CONNECTION_TIMED_OUT, true);
    checkSpecificErrorCode(NetError.ERR_TIMED_OUT, NetworkException.ERROR_TIMED_OUT, true);
    checkSpecificErrorCode(NetError.ERR_ADDRESS_UNREACHABLE,
                           NetworkException.ERROR_ADDRESS_UNREACHABLE, false);
    // TODO("enable")
    // BidirectionalStream specific retryable error codes.
    // checkSpecificErrorCode(NetError.ERR_HTTP2_PING_FAILED, NetworkException.ERROR_OTHER, true);
    // checkSpecificErrorCode(
    //        NetError.ERR_QUIC_HANDSHAKE_FAILED, NetworkException.ERROR_OTHER, true);
  }

  // Returns the contents of byteBuffer, from its position() to its limit(),
  // as a String. Does not modify byteBuffer's position().
  private static String bufferContentsToString(ByteBuffer byteBuffer, int start, int end) {
    // Use a duplicate to avoid modifying byteBuffer.
    ByteBuffer duplicate = byteBuffer.duplicate();
    duplicate.position(start);
    duplicate.limit(end);
    byte[] contents = new byte[duplicate.remaining()];
    duplicate.get(contents);
    return new String(contents);
  }

  private static void checkSpecificErrorCode(int netError, int errorCode,
                                             boolean immediatelyRetryable) throws Exception {
    NetworkException exception = new BidirectionalStreamNetworkException("", errorCode, netError);
    assertEquals(immediatelyRetryable, exception.immediatelyRetryable());
    assertEquals(netError, exception.getCronetInternalErrorCode());
    assertEquals(errorCode, exception.getErrorCode());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @RequiresMinApi(10) // Tagging support added in API level 10: crrev.com/c/chromium/src/+/937583
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1521")
  public void testTagging() throws Exception {
    if (!CronetTestUtil.nativeCanGetTaggedBytes()) {
      Log.i(TAG, "Skipping test - GetTaggedBytes unsupported.");
      return;
    }
    String url = Http2TestServer.getEchoStreamUrl();

    // Test untagged requests are given tag 0.
    int tag = 0;
    long priorBytes = CronetTestUtil.nativeGetTaggedBytes(tag);
    TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
    callback.addWriteData(new byte[] {0});
    mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor())
        .build()
        .start();
    callback.blockForDone();
    assertTrue(CronetTestUtil.nativeGetTaggedBytes(tag) > priorBytes);

    // Test explicit tagging.
    tag = 0x12345678;
    priorBytes = CronetTestUtil.nativeGetTaggedBytes(tag);
    callback = new TestBidirectionalStreamCallback();
    callback.addWriteData(new byte[] {0});
    ExperimentalBidirectionalStream.Builder builder =
        mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor());
    assertEquals(builder.setTrafficStatsTag(tag), builder);
    builder.build().start();
    callback.blockForDone();
    assertTrue(CronetTestUtil.nativeGetTaggedBytes(tag) > priorBytes);

    // Test a different tag value to make sure reused connections are retagged.
    tag = 0x87654321;
    priorBytes = CronetTestUtil.nativeGetTaggedBytes(tag);
    callback = new TestBidirectionalStreamCallback();
    callback.addWriteData(new byte[] {0});
    builder = mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor());
    assertEquals(builder.setTrafficStatsTag(tag), builder);
    builder.build().start();
    callback.blockForDone();
    assertTrue(CronetTestUtil.nativeGetTaggedBytes(tag) > priorBytes);

    // Test tagging with our UID.
    tag = 0;
    priorBytes = CronetTestUtil.nativeGetTaggedBytes(tag);
    callback = new TestBidirectionalStreamCallback();
    callback.addWriteData(new byte[] {0});
    builder = mCronetEngine.newBidirectionalStreamBuilder(url, callback, callback.getExecutor());
    assertEquals(builder.setTrafficStatsUid(Process.myUid()), builder);
    builder.build().start();
    callback.blockForDone();
    assertTrue(CronetTestUtil.nativeGetTaggedBytes(tag) > priorBytes);
  }
}
