package org.chromium.net.urlconnection;

import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;
import java.io.IOException;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.ProtocolException;
import java.net.URL;
import org.chromium.net.CronetEngine;
import org.chromium.net.NetworkException;
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

/**
 * Tests {@code getOutputStream} when {@code setChunkedStreamingMode} is enabled.
 * Tests annotated with {@code CompareDefaultWithCronet} will run once with the
 * default HttpURLConnection implementation and then with Cronet's
 * HttpURLConnection implementation. Tests annotated with
 * {@code OnlyRunCronetHttpURLConnection} only run Cronet's implementation.
 * See {@link CronetTestRule#runBase()} ()} for details.
 */
@RunWith(AndroidJUnit4.class)
public class CronetChunkedOutputStreamTest {
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
    connection.setChunkedStreamingMode(0);
    assertEquals(200, connection.getResponseCode());
    try {
      connection.getOutputStream();
      fail();
    } catch (ProtocolException e) {
      // Expected.
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testWriteAfterReadingResponse() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setChunkedStreamingMode(0);
    OutputStream out = connection.getOutputStream();
    assertEquals(200, connection.getResponseCode());
    try {
      out.write(TestUtil.UPLOAD_DATA);
      fail();
    } catch (IOException e) {
      // Expected.
    }
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
    connection.setChunkedStreamingMode(0);
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    NativeTestServer.shutdownNativeTestServer();
    try {
      out.write(TestUtil.getLargeData());
      connection.getResponseCode();
      fail();
    } catch (IOException e) {
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
    // Set 1 byte as chunk size so internally Cronet will try upload when
    // 1 byte is filled.
    connection.setChunkedStreamingMode(1);
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
  public void testPost() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setChunkedStreamingMode(0);
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
  public void testTransferEncodingHeaderSet() throws Exception {
    URL url = new URL(NativeTestServer.getEchoHeaderURL("Transfer-Encoding"));
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setChunkedStreamingMode(0);
    OutputStream out = connection.getOutputStream();
    out.write(TestUtil.UPLOAD_DATA);
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    assertEquals("chunked", TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @CompareDefaultWithCronet
  public void testPostOneMassiveWrite() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setChunkedStreamingMode(0);
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
  public void testPostWriteOneByte() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setChunkedStreamingMode(0);
    OutputStream out = connection.getOutputStream();
    for (int i = 0; i < TestUtil.UPLOAD_DATA.length; i++) {
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
  public void testPostOneMassiveWriteWriteOneByte() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    connection.setChunkedStreamingMode(0);
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
  public void testPostWholeNumberOfChunks() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    int totalSize = TestUtil.UPLOAD_DATA.length * TestUtil.REPEAT_COUNT;
    int chunkSize = 18000;
    // Ensure total data size is a multiple of chunk size, so no partial
    // chunks will be used.
    assertEquals(0, totalSize % chunkSize);
    connection.setChunkedStreamingMode(chunkSize);
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
  @OnlyRunCronetHttpURLConnection
  // Regression testing for crbug.com/618872.
  public void testOneMassiveWriteLargerThanInternalBuffer() throws Exception {
    URL url = new URL(NativeTestServer.getEchoBodyURL());
    HttpURLConnection connection = (HttpURLConnection)url.openConnection();
    connection.setDoOutput(true);
    connection.setRequestMethod("POST");
    // Use a super big chunk size so that it exceeds the UploadDataProvider
    // read buffer size.
    byte[] largeData = TestUtil.getLargeData();
    connection.setChunkedStreamingMode(largeData.length);
    OutputStream out = connection.getOutputStream();
    out.write(largeData);
    assertEquals(200, connection.getResponseCode());
    TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }
}
