package org.chromium.net.urlconnection;

import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import androidx.test.filters.SmallTest;
import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import org.chromium.net.CronetEngine;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CompareDefaultWithCronet;
import org.chromium.net.testing.CronetTestRule.OnlyRunCronetHttpURLConnection;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.NativeTestServer;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Tests the CronetBufferedOutputStream implementation.
 */
@RunWith(RobolectricTestRunner.class)
public class CronetBufferedOutputStreamTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

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
  public void testGetOutputStreamAfterConnectionMade() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    assertEquals(200, connection.getResponseCode());
    try {
      connection.getOutputStream();
      fail();
    } catch (java.net.ProtocolException e) {
      // Expected.
    }
  }

  /**
   * Tests write after connect. Strangely, the default implementation allows
   * writing after being connected, so this test only runs against Cronet 's
   * implementation.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunCronetHttpURLConnection
  public void testWriteAfterConnect() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    connection.connect();
    try {
      // Attempt to write some more.
      out.write(TestUtil.UPLOAD_DATA);
      fail();
    } catch (IllegalStateException e) {
      assertEquals("Use setFixedLengthStreamingMode() or "
                       + "setChunkedStreamingMode() for writing after connect",
                   e.getMessage());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1553")
  public void testWriteAfterReadingResponse() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    OutputStream out = connection.getOutputStream();
    assertEquals(200, connection.getResponseCode());
    try {
      out.write(TestUtil.UPLOAD_DATA);
      fail();
    } catch (Exception e) {
      // Default implementation gives an IOException and says that the
      // stream is closed. Cronet gives an IllegalStateException and
      // complains about write after connected.
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testPostWithContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    byte[] largeData = TestUtil.getLargeData();
    connection.setRequestProperty("Content-Length", Integer.toString(largeData.length));
    OutputStream out = connection.getOutputStream();
    int totalBytesWritten = 0;
    // Number of bytes to write each time. It is doubled each time
    // to make sure that the buffer grows.
    int bytesToWrite = 683;
    while (totalBytesWritten < largeData.length) {
      if (bytesToWrite > largeData.length - totalBytesWritten) {
        // Do not write out of bound.
        bytesToWrite = largeData.length - totalBytesWritten;
      }
      out.write(largeData, totalBytesWritten, bytesToWrite);
      totalBytesWritten += bytesToWrite;
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
  public void testPostWithContentLengthOneMassiveWrite() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    byte[] largeData = TestUtil.getLargeData();
    connection.setRequestProperty("Content-Length", Integer.toString(largeData.length));
    OutputStream out = connection.getOutputStream();
    out.write(largeData);
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testPostWithContentLengthWriteOneByte() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    byte[] largeData = TestUtil.getLargeData();
    connection.setRequestProperty("Content-Length", Integer.toString(largeData.length));
    OutputStream out = connection.getOutputStream();
    for (int i = 0; i < largeData.length; i++) {
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
  @CompareDefaultWithCronet
  public void testPostWithZeroContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setRequestProperty("Content-Length", "0");
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    assertEquals("", TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testPostZeroByteWithoutContentLength() throws Exception {
    // Make sure both implementation sets the Content-Length header to 0.
    // TODO(colibie) added the if-statement because sdk 29 impl does not set content-Length to zero
    if (!mTestRule.testingSystemHttpURLConnection()) {
      URL url = new URL(NativeTestServer.getEchoHeaderURL("Content-Length"));
      HttpURLConnection connection = (HttpURLConnection)url.openConnection();
      connection.setDoOutput(true);
      connection.setRequestMethod("POST");
      assertEquals(200, connection.getResponseCode());
      assertEquals("OK", connection.getResponseMessage());
      assertEquals("0", TestUtil.getResponseAsString(connection));
      connection.disconnect();
    }

    // Make sure the server echoes back empty body for both implementation.
    URL echoBody = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection2 = (HttpURLConnection)echoBody.openConnection();
    connection2.setDoOutput(true);
    connection2.setRequestMethod("POST");
    assertEquals(200, connection2.getResponseCode());
    assertEquals("OK", connection2.getResponseMessage());
    assertEquals("", TestUtil.getResponseAsString(connection2));
    connection2.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testPostWithoutContentLengthSmall() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
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
  public void testPostWithoutContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    byte[] largeData = TestUtil.getLargeData();
    OutputStream out = connection.getOutputStream();
    int totalBytesWritten = 0;
    // Number of bytes to write each time. It is doubled each time
    // to make sure that the buffer grows.
    int bytesToWrite = 683;
    while (totalBytesWritten < largeData.length) {
      if (bytesToWrite > largeData.length - totalBytesWritten) {
        // Do not write out of bound.
        bytesToWrite = largeData.length - totalBytesWritten;
      }
      out.write(largeData, totalBytesWritten, bytesToWrite);
      totalBytesWritten += bytesToWrite;
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
  public void testPostWithoutContentLengthOneMassiveWrite() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    OutputStream out = connection.getOutputStream();
    byte[] largeData = TestUtil.getLargeData();
    out.write(largeData);
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testPostWithoutContentLengthWriteOneByte() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    OutputStream out = connection.getOutputStream();
    byte[] largeData = TestUtil.getLargeData();
    for (int i = 0; i < largeData.length; i++) {
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
  @CompareDefaultWithCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1553")
  public void testWriteLessThanContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // Set a content length that's 1 byte more.
    connection.setRequestProperty("Content-Length",
                                  Integer.toString(TestUtil.UPLOAD_DATA.length + 1));
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

  /**
   * Tests that if caller writes more than the content length provided,
   * an exception should occur.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  @Ignore("https://github.com/envoyproxy/envoy-mobile/issues/1553")
  public void testWriteMoreThanContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // Use a content length that is 1 byte shorter than actual data.
    connection.setRequestProperty("Content-Length",
                                  Integer.toString(TestUtil.UPLOAD_DATA.length - 1));
    OutputStream out = connection.getOutputStream();
    // Write a few bytes first.
    out.write(TestUtil.UPLOAD_DATA, 0, 3);
    try {
      // Write remaining bytes.
      out.write(TestUtil.UPLOAD_DATA, 3, TestUtil.UPLOAD_DATA.length - 3);
      // On Lollipop, default implementation only triggers the error when reading response.
      connection.getInputStream();
      fail();
    } catch (IOException e) {
      assertEquals("exceeded content-length limit of " + (TestUtil.UPLOAD_DATA.length - 1) +
                       " bytes",
                   e.getMessage());
    }
    connection.disconnect();
  }

  /**
   * Same as {@code testWriteMoreThanContentLength()}, but it only writes one byte
   * at a time.
   */
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
    // Use a content length that is 1 byte shorter than actual data.
    connection.setRequestProperty("Content-Length",
                                  Integer.toString(TestUtil.UPLOAD_DATA.length - 1));
    OutputStream out = connection.getOutputStream();
    try {
      for (int i = 0; i < TestUtil.UPLOAD_DATA.length; i++) {
        out.write(TestUtil.UPLOAD_DATA[i]);
      }
      // On Lollipop, default implementation only triggers the error when reading response.
      connection.getInputStream();
      fail();
    } catch (IOException e) {
      assertEquals("exceeded content-length limit of " + (TestUtil.UPLOAD_DATA.length - 1) +
                       " bytes",
                   e.getMessage());
    }
    connection.disconnect();
  }

  /**
   * Tests that {@link CronetBufferedOutputStream} supports rewind in a
   * POST preserving redirect.
   * Use {@code OnlyRunCronetHttpURLConnection} as the default implementation
   * does not pass this test.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunCronetHttpURLConnection
  public void testRewind() throws Exception {
    URL url = new URL(NativeTestServer.getRedirectToEchoBody());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setRequestProperty("Content-Length", Integer.toString(TestUtil.UPLOAD_DATA.length));
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  /**
   * Like {@link #testRewind} but does not set Content-Length header.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunCronetHttpURLConnection
  public void testRewindWithoutContentLength() throws Exception {
    URL url = new URL(NativeTestServer.getRedirectToEchoBody());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }
}
