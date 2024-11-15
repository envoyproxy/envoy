package org.chromium.net.urlconnection;

import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import androidx.test.filters.SmallTest;
import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpRetryException;
import java.net.HttpURLConnection;
import java.net.URL;
import org.chromium.net.CronetEngine;
import org.chromium.net.NetworkException;
import org.chromium.net.impl.CronvoyCallbackExceptionImpl;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CompareDefaultWithCronet;
import org.chromium.net.testing.CronetTestRule.OnlyRunCronetHttpURLConnection;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.NativeTestServer;
import org.hamcrest.Description;
import org.hamcrest.TypeSafeMatcher;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Tests {@code getOutputStream} when {@code setFixedLengthStreamingMode} is
 * enabled.
 * Tests annotated with {@code CompareDefaultWithCronet} will run once with the
 * default HttpURLConnection implementation and then with Cronet's
 * HttpURLConnection implementation. Tests annotated with
 * {@code OnlyRunCronetHttpURLConnection} only run Cronet's implementation.
 * See {@link CronetTestRule#runBase()} ()} for details.
 */
@RunWith(RobolectricTestRunner.class)
public class CronetFixedModeOutputStreamTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  @Rule public ExpectedException thrown = ExpectedException.none();

  @Before
  public void setUp() throws Exception {
    mTestRule.setStreamHandlerFactory(new CronetEngine.Builder(getContext()).build());
    assertTrue(NativeTestServer.startNativeTestServer(getContext()));
  }

  @After
  public void tearDown() throws Exception {
    NativeTestServer.shutdownNativeTestServer();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testConnectBeforeWrite() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
    OutputStream out = connection.getOutputStream();
    connection.connect();
    out.write(TestUtil.UPLOAD_DATA);
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  // Regression test for crbug.com/687600.
  public void testZeroLengthWriteWithNoResponseBody() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setFixedLengthStreamingMode(0);
    OutputStream out = connection.getOutputStream();
    out.write(new byte[] {});
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1550")
  public void testWriteAfterRequestFailed() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    byte[] largeData = TestUtil.getLargeData();
    connection.setFixedLengthStreamingMode(largeData.length);
    OutputStream out = connection.getOutputStream();
    out.write(largeData, 0, 10);
    NativeTestServer.shutdownNativeTestServer();
    try {
      out.write(largeData, 10, largeData.length - 10);
      connection.getResponseCode();
      fail();
    } catch (IOException e) {
      // Expected.
      if (!mTestRule.testingSystemHttpURLConnection()) {
        NetworkException requestException = (NetworkException)e;
        assertEquals(NetworkException.ERROR_CONNECTION_REFUSED, requestException.getErrorCode());
      }
    }
    connection.disconnect();
    // Restarting server to run the test for a second time.
    assertTrue(NativeTestServer.startNativeTestServer(getContext()));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1550")
  public void testGetResponseAfterWriteFailed() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    NativeTestServer.shutdownNativeTestServer();
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // Set content-length as 1 byte, so Cronet will upload once that 1 byte
    // is passed to it.
    connection.setFixedLengthStreamingMode(1);
    try {
      OutputStream out = connection.getOutputStream();
      out.write(1);
      out.write(1);
      // Forces OutputStream implementation to flush. crbug.com/653072
      out.flush();
      // System's implementation is flaky see crbug.com/653072.
      if (!mTestRule.testingSystemHttpURLConnection()) {
        fail();
      }
    } catch (IOException e) {
      if (!mTestRule.testingSystemHttpURLConnection()) {
        NetworkException requestException = (NetworkException)e;
        assertEquals(NetworkException.ERROR_CONNECTION_REFUSED, requestException.getErrorCode());
      }
    }
    // Make sure IOException is reported again when trying to read response
    // from the connection.
    try {
      connection.getResponseCode();
      fail();
    } catch (IOException e) {
      // Expected.
      if (!mTestRule.testingSystemHttpURLConnection()) {
        NetworkException requestException = (NetworkException)e;
        assertEquals(NetworkException.ERROR_CONNECTION_REFUSED, requestException.getErrorCode());
      }
    }
    // Restarting server to run the test for a second time.
    assertTrue(NativeTestServer.startNativeTestServer(getContext()));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testFixedLengthStreamingModeZeroContentLength() throws Exception {
    // Check content length is set.
    URL echoLength = new URL(NativeTestServer.getEchoHeaderURL("Content-Length"));
    HttpURLConnection connection1 = (HttpURLConnection)echoLength.openConnection();
    connection1.setDoOutput(true);
    connection1.setRequestMethod("POST");
    connection1.setFixedLengthStreamingMode(0);
    assertEquals(200, connection1.getResponseCode());
    assertEquals("OK", connection1.getResponseMessage());
    assertEquals("0", TestUtil.getResponseAsString(connection1));
    connection1.disconnect();

    // Check body is empty.
    URL echoBody = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection2 = (HttpURLConnection)echoBody.openConnection();
    connection2.setDoOutput(true);
    connection2.setRequestMethod("POST");
    connection2.setFixedLengthStreamingMode(0);
    assertEquals(200, connection2.getResponseCode());
    assertEquals("OK", connection2.getResponseMessage());
    assertEquals("", TestUtil.getResponseAsString(connection2));
    connection2.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testWriteLessThanContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // Set a content length that's 1 byte more.
    connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length + 1);
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    try {
      connection.getResponseCode();
      fail();
    } catch (IOException e) {
      // Expected.
    }
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testWriteMoreThanContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // Set a content length that's 1 byte short.
    connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length - 1);
    OutputStream out = connection.getOutputStream();
    try {
      out.write(TestUtil.UPLOAD_DATA);
      // On Lollipop, default implementation only triggers the error when reading response.
      connection.getInputStream();
      fail();
    } catch (IOException e) {
      // Expected.
      // TODO (colibie) run by cleborgne@
      String expectedVariant = "expected " + (TestUtil.UPLOAD_DATA.length - 1) +
                               " bytes but received " + TestUtil.UPLOAD_DATA.length;
      String expectedVariantOnAndroid10 = "too many bytes written";
      assertTrue(expectedVariant.equals(e.getMessage()) ||
                 expectedVariantOnAndroid10.equals(e.getMessage()));
    }
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1553")
  public void testWriteMoreThanContentLengthWriteOneByte() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // Set a content length that's 1 byte short.
    connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length - 1);
    OutputStream out = connection.getOutputStream();
    for (int i = 0; i < TestUtil.UPLOAD_DATA.length - 1; i++) {
      out.write(TestUtil.UPLOAD_DATA[i]);
    }
    try {
      // Try upload an extra byte.
      out.write(TestUtil.UPLOAD_DATA[TestUtil.UPLOAD_DATA.length - 1]);
      // On Lollipop, default implementation only triggers the error when reading response.
      connection.getInputStream();
      fail();
    } catch (IOException e) {
      // Expected.
      String expectedVariant = "expected 0 bytes but received 1";
      String expectedVariantOnLollipop = "expected " + (TestUtil.UPLOAD_DATA.length - 1) +
                                         " bytes but received " + TestUtil.UPLOAD_DATA.length;
      assertTrue(expectedVariant.equals(e.getMessage()) ||
                 expectedVariantOnLollipop.equals(e.getMessage()));
    }
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testFixedLengthStreamingMode() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testFixedLengthStreamingModeWriteOneByte() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
    OutputStream out = connection.getOutputStream();
    for (int i = 0; i < TestUtil.UPLOAD_DATA.length; i++) {
      // Write one byte at a time.
      out.write(TestUtil.UPLOAD_DATA[i]);
    }
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testFixedLengthStreamingModeLargeData() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // largeData is 1.8 MB.
    byte[] largeData = TestUtil.getLargeData();
    connection.setFixedLengthStreamingMode(largeData.length);
    OutputStream out = connection.getOutputStream();
    int totalBytesWritten = 0;
    // Number of bytes to write each time. It is doubled each time
    // to make sure that the implementation can handle large writes.
    int bytesToWrite = 683;
    while (totalBytesWritten < largeData.length) {
      if (bytesToWrite > largeData.length - totalBytesWritten) {
        // Do not write out of bound.
        bytesToWrite = largeData.length - totalBytesWritten;
      }
      out.write(largeData, totalBytesWritten, bytesToWrite);
      totalBytesWritten += bytesToWrite;
      // About 5th iteration of this loop, bytesToWrite will be bigger than 16384.
      bytesToWrite *= 2;
    }
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testFixedLengthStreamingModeLargeDataWriteOneByte() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    byte[] largeData = TestUtil.getLargeData();
    connection.setFixedLengthStreamingMode(largeData.length);
    OutputStream out = connection.getOutputStream();
    for (int i = 0; i < largeData.length; i++) {
      // Write one byte at a time.
      out.write(largeData[i]);
    }
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunCronetHttpURLConnection
  public void testJavaBufferSizeLargerThanNativeBufferSize() throws Exception {
    // Set an internal buffer of size larger than the buffer size used
    // in network stack internally.
    // Normal stream uses 16384, QUIC uses 14520, and SPDY uses 16384.
    // Try two different buffer lengths. 17384 will make the last write
    // smaller than the native buffer length; 18384 will make the last write
    // bigger than the native buffer length
    // (largeData.length % 17384 = 9448, largeData.length % 18384 = 16752).
    int[] bufferLengths = new int[] {17384, 18384};
    for (int length : bufferLengths) {
      CronvoyFixedModeOutputStream.setDefaultBufferLengthForTesting(length);
      // Run the following three tests with this custom buffer size.
      testFixedLengthStreamingModeLargeDataWriteOneByte();
      testFixedLengthStreamingModeLargeData();
      testOneMassiveWrite();
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testOneMassiveWrite() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    byte[] largeData = TestUtil.getLargeData();
    connection.setFixedLengthStreamingMode(largeData.length);
    OutputStream out = connection.getOutputStream();
    // Write everything at one go, so the data is larger than the buffer
    // used in CronetFixedModeOutputStream.
    out.write(largeData);
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  private static class CauseMatcher extends TypeSafeMatcher<Throwable> {
    private final Class<? extends Throwable> mType;
    private final String mExpectedMessage;

    public CauseMatcher(Class<? extends Throwable> type, String expectedMessage) {
      this.mType = type;
      this.mExpectedMessage = expectedMessage;
    }

    @Override
    protected boolean matchesSafely(Throwable item) {
      return item.getClass().isAssignableFrom(mType) && item.getMessage().equals(mExpectedMessage);
    }
    @Override
    public void describeTo(Description description) {}
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunCronetHttpURLConnection
  public void testRewindWithCronet() throws Exception {
    assertFalse(mTestRule.testingSystemHttpURLConnection());
    // Post preserving redirect should fail.
    URL url = new URL(NativeTestServer.getRedirectToEchoBody());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
    thrown.expect(instanceOf(CronvoyCallbackExceptionImpl.class));
    thrown.expectMessage("Exception received from UploadDataProvider");
    thrown.expectCause(
        new CauseMatcher(HttpRetryException.class, "Cannot retry streamed Http body"));
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    connection.getResponseCode();
    connection.disconnect();
  }
}
