package org.chromium.net.urlconnection;

import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import androidx.test.filters.SmallTest;
import java.net.HttpURLConnection;
import java.net.InetSocketAddress;
import java.net.Proxy;
import java.net.URL;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CronetTestFramework;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.NativeTestServer;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Tests for CronetHttpURLStreamHandler class.
 */
@RunWith(RobolectricTestRunner.class)
public class CronetHttpURLStreamHandlerTest {
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
  @SmallTest
  @Feature({"Cronet"})
  public void testOpenConnectionHttp() throws Exception {
    URL url = new URL(NativeTestServer.getEchoMethodURL());
    CronvoyHttpURLStreamHandler streamHandler =
        new CronvoyHttpURLStreamHandler(mTestFramework.mCronetEngine);
    HttpURLConnection connection = (HttpURLConnection)streamHandler.openConnection(url);
    assertEquals(200, connection.getResponseCode());
    assertEquals("OK", connection.getResponseMessage());
    assertEquals("GET", TestUtil.getResponseAsString(connection));
    connection.disconnect();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testOpenConnectionHttps() throws Exception {
    URL url = new URL("https://example.com");
    CronvoyHttpURLStreamHandler streamHandler =
        new CronvoyHttpURLStreamHandler(mTestFramework.mCronetEngine);
    HttpURLConnection connection = (HttpURLConnection)streamHandler.openConnection(url);
    assertNotNull(connection);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testOpenConnectionProtocolNotSupported() throws Exception {
    URL url = new URL("ftp://example.com");
    CronvoyHttpURLStreamHandler streamHandler =
        new CronvoyHttpURLStreamHandler(mTestFramework.mCronetEngine);
    try {
      streamHandler.openConnection(url);
      fail();
    } catch (UnsupportedOperationException e) {
      assertEquals("Unexpected protocol:ftp", e.getMessage());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testOpenConnectionWithProxy() throws Exception {
    URL url = new URL(NativeTestServer.getEchoMethodURL());
    CronvoyHttpURLStreamHandler streamHandler =
        new CronvoyHttpURLStreamHandler(mTestFramework.mCronetEngine);
    Proxy proxy = new Proxy(Proxy.Type.HTTP, new InetSocketAddress("127.0.0.1", 8080));
    try {
      streamHandler.openConnection(url, proxy);
      fail();
    } catch (UnsupportedOperationException e) {
      // Expected.
    }
  }
}
