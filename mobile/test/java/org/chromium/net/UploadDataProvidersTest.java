package org.chromium.net;

import static org.chromium.net.testing.CronetTestRule.assertContains;
import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.os.ConditionVariable;
import android.os.ParcelFileDescriptor;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CronetTestFramework;
import org.chromium.net.testing.CronetTestRule.OnlyRunNativeCronet;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.NativeTestServer;
import org.chromium.net.testing.TestUrlRequestCallback;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Test the default provided implementations of {@link UploadDataProvider} */
@RunWith(AndroidJUnit4.class)
public class UploadDataProvidersTest {
  private static final String LOREM =
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
      + "Proin elementum, libero laoreet fringilla faucibus, metus tortor vehicula ante, "
      + "lacinia lorem eros vel sapien.";
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();
  private CronetTestFramework mTestFramework;
  private File mFile;

  @Before
  public void setUp() throws Exception {
    mTestFramework = mTestRule.startCronetTestFramework();
    assertTrue(NativeTestServer.startNativeTestServer(getContext()));
    // Add url interceptors after native application context is initialized.
    mFile = new File(getContext().getCacheDir().getPath() + "/tmpfile");
    FileOutputStream fileOutputStream = new FileOutputStream(mFile);
    try {
      fileOutputStream.write(LOREM.getBytes("UTF-8"));
    } finally {
      fileOutputStream.close();
    }
  }

  @After
  public void tearDown() {
    NativeTestServer.shutdownNativeTestServer();
    assertTrue(mFile.delete());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testFileProvider() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());
    UploadDataProvider dataProvider = UploadDataProviders.create(mFile);
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("Content-Type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(LOREM, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testFileDescriptorProvider() throws Exception {
    ParcelFileDescriptor descriptor =
        ParcelFileDescriptor.open(mFile, ParcelFileDescriptor.MODE_READ_ONLY);
    assertTrue(descriptor.getFileDescriptor().valid());
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());
    UploadDataProvider dataProvider = UploadDataProviders.create(descriptor);
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("Content-Type", "useless/string");
    builder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(LOREM, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Needs more investigation")
  public void testBadFileDescriptorProvider() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());
    ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
    try {
      // see uploadDataProvidersL38: fd.getStatSize() does not return same response as in cronet
      UploadDataProvider dataProvider = UploadDataProviders.create(pipe[0]);
      builder.setUploadDataProvider(dataProvider, callback.getExecutor());
      builder.addHeader("Content-Type", "useless/string");
      builder.build().start();
      callback.blockForDone();

      assertTrue(callback.mError.getCause() instanceof IllegalArgumentException);
    } finally {
      pipe[1].close();
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBufferProvider() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());
    UploadDataProvider dataProvider = UploadDataProviders.create(LOREM.getBytes("UTF-8"));
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    builder.addHeader("Content-Type", "useless/string");
    builder.build().start();
    callback.blockForDone();

    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(LOREM, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  // Tests that ByteBuffer's limit cannot be changed by the caller.
  @Ignore("Blocked by envoy-mobile flow control impl. ByteBuffer impl for cronvoy is different")
  public void testUploadChangeBufferLimit() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());
    builder.addHeader("Content-Type", "useless/string");
    builder.setUploadDataProvider(new UploadDataProvider() {
      private static final String CONTENT = "hello";
      @Override
      public long getLength() throws IOException {
        return CONTENT.length();
      }

      @Override
      public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) throws IOException {
        int oldPos = byteBuffer.position();
        int oldLimit = byteBuffer.limit();
        byteBuffer.put(CONTENT.getBytes());
        assertEquals(oldPos + CONTENT.length(), byteBuffer.position());
        assertEquals(oldLimit, byteBuffer.limit());
        // Now change the limit to something else. This should give an error.
        byteBuffer.limit(oldLimit - 1);
        uploadDataSink.onReadSucceeded(false);
      }

      @Override
      public void rewind(UploadDataSink uploadDataSink) throws IOException {}
    }, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.blockForDone();
    assertTrue(callback.mOnErrorCalled);
    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains("ByteBuffer limit changed", callback.mError.getCause().getMessage());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testNoErrorWhenCanceledDuringStart() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());
    final ConditionVariable first = new ConditionVariable();
    final ConditionVariable second = new ConditionVariable();
    builder.addHeader("Content-Type", "useless/string");
    builder.setUploadDataProvider(new UploadDataProvider() {
      @Override
      public long getLength() throws IOException {
        first.open();
        second.block();
        return 0;
      }

      @Override
      public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) throws IOException {}

      @Override
      public void rewind(UploadDataSink uploadDataSink) throws IOException {}
    }, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    first.block();
    urlRequest.cancel();
    second.open();
    callback.blockForDone();
    assertTrue(callback.mOnCanceledCalled);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testNoErrorWhenExceptionDuringStart() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoBodyURL(), callback, callback.getExecutor());
    final ConditionVariable first = new ConditionVariable();
    final String exceptionMessage = "Bad Length";
    builder.addHeader("Content-Type", "useless/string");
    builder.setUploadDataProvider(new UploadDataProvider() {
      @Override
      public long getLength() throws IOException {
        first.open();
        throw new IOException(exceptionMessage);
      }

      @Override
      public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) throws IOException {}
      @Override
      public void rewind(UploadDataSink uploadDataSink) throws IOException {}
    }, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    first.block();
    callback.blockForDone();
    assertFalse(callback.mOnCanceledCalled);
    assertTrue(callback.mError instanceof CallbackException);
    assertContains("Exception received from UploadDataProvider", callback.mError.getMessage());
    assertContains(exceptionMessage, callback.mError.getCause().getMessage());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  // Tests that creating a ByteBufferUploadProvider using a byte array with an
  // offset gives a ByteBuffer with position 0. crbug.com/603124.
  public void testCreateByteBufferUploadWithArrayOffset() throws Exception {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    // This URL will trigger a rewind().
    UrlRequest.Builder builder = mTestFramework.mCronetEngine.newUrlRequestBuilder(
        NativeTestServer.getRedirectToEchoBody(), callback, callback.getExecutor());
    builder.addHeader("Content-Type", "useless/string");
    byte[] uploadData = LOREM.getBytes("UTF-8");
    int offset = 5;
    byte[] uploadDataWithPadding = new byte[uploadData.length + offset];
    System.arraycopy(uploadData, 0, uploadDataWithPadding, offset, uploadData.length);
    UploadDataProvider dataProvider =
        UploadDataProviders.create(uploadDataWithPadding, offset, uploadData.length);
    assertEquals(uploadData.length, dataProvider.getLength());
    builder.setUploadDataProvider(dataProvider, callback.getExecutor());
    UrlRequest urlRequest = builder.build();
    urlRequest.start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertEquals(LOREM, callback.mResponseAsString);
  }
}
