package org.chromium.net;

import static org.chromium.net.CronetEngine.Builder.HTTP_CACHE_IN_MEMORY;
import static org.chromium.net.testing.CronetTestRule.assertContains;
import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.chromium.net.testing.CronetTestRule.getTestStorage;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import android.content.Context;
import android.content.ContextWrapper;
import android.os.ConditionVariable;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.concurrent.Callable;
import java.util.concurrent.Executor;
import java.util.concurrent.FutureTask;
import java.util.concurrent.atomic.AtomicReference;
import org.chromium.net.impl.CronetUrlRequestContext;
import org.chromium.net.impl.NativeCronetEngineBuilderImpl;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.CronetTestFramework;
import org.chromium.net.testing.CronetTestRule.OnlyRunNativeCronet;
import org.chromium.net.testing.CronetTestRule.RequiresMinApi;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.FileUtils;
import org.chromium.net.testing.NativeTestServer;
import org.chromium.net.testing.PathUtils;
import org.chromium.net.testing.TestUrlRequestCallback;
import org.chromium.net.testing.TestUrlRequestCallback.ResponseStep;
import org.json.JSONObject;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Test CronetEngine.
 */
@RunWith(AndroidJUnit4.class)
public class CronetUrlRequestContextTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private static final String TAG = CronetUrlRequestContextTest.class.getSimpleName();

  // URLs used for tests.
  private static final String MOCK_CRONET_TEST_FAILED_URL = "http://mock.failed.request/-2";
  private static final String MOCK_CRONET_TEST_SUCCESS_URL = "http://mock.http/success.txt";
  private static final int MAX_FILE_SIZE = 1000000000;
  private static final int NUM_EVENT_FILES = 10;

  private String mUrl;
  private String mUrl404;
  private String mUrl500;

  @Before
  public void setUp() {
    NativeTestServer.startNativeTestServer(getContext());
    mUrl = NativeTestServer.getSuccessURL();
    mUrl404 = NativeTestServer.getNotFoundURL();
    mUrl500 = NativeTestServer.getInternalErrorURL();
  }

  @After
  public void tearDown() {
    NativeTestServer.shutdownNativeTestServer();
  }

  static class RequestThread extends Thread {
    public TestUrlRequestCallback mCallback;

    final String mUrl;
    final ConditionVariable mRunBlocker;

    public RequestThread(String url, ConditionVariable runBlocker) {
      mUrl = url;
      mRunBlocker = runBlocker;
    }

    @Override
    public void run() {
      mRunBlocker.block();
      CronetEngine cronetEngine = new CronetEngine.Builder(getContext()).build();
      mCallback = new TestUrlRequestCallback();
      UrlRequest.Builder urlRequestBuilder =
          cronetEngine.newUrlRequestBuilder(mUrl, mCallback, mCallback.getExecutor());
      urlRequestBuilder.build().start();
      mCallback.blockForDone();
      cronetEngine.shutdown();
    }
  }

  /**
   * Callback that shutdowns the request context when request has succeeded
   * or failed.
   */
  static class ShutdownTestUrlRequestCallback extends TestUrlRequestCallback {
    private final CronetEngine mCronetEngine;
    private final ConditionVariable mCallbackCompletionBlock = new ConditionVariable();

    ShutdownTestUrlRequestCallback(CronetEngine cronetEngine) { mCronetEngine = cronetEngine; }

    @Override
    public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
      super.onSucceeded(request, info);
      mCronetEngine.shutdown();
      mCallbackCompletionBlock.open();
    }

    @Override
    public void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error) {
      super.onFailed(request, info, error);
      mCronetEngine.shutdown();
      mCallbackCompletionBlock.open();
    }

    // Wait for request completion callback.
    void blockForCallbackToComplete() { mCallbackCompletionBlock.block(); }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testConfigUserAgent() throws Exception {
    String userAgentName = "User-Agent";
    String userAgentValue = "User-Agent-Value";
    ExperimentalCronetEngine.Builder cronetEngineBuilder =
        new ExperimentalCronetEngine.Builder(getContext());
    if (mTestRule.testingJavaImpl()) {
      cronetEngineBuilder = mTestRule.createJavaEngineBuilder();
    }
    cronetEngineBuilder.setUserAgent(userAgentValue);
    final CronetEngine cronetEngine = cronetEngineBuilder.build();
    NativeTestServer.shutdownNativeTestServer(); // startNativeTestServer returns false if it's
    // already running
    assertTrue(NativeTestServer.startNativeTestServer(getContext()));
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder = cronetEngine.newUrlRequestBuilder(
        NativeTestServer.getEchoHeaderURL(userAgentName), callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertEquals(userAgentValue, callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  // TODO: Remove the annotation after fixing http://crbug.com/637979 & http://crbug.com/637972
  @OnlyRunNativeCronet
  @Ignore("Properly implement shutdown sequence")
  public void testShutdown() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    ShutdownTestUrlRequestCallback callback =
        new ShutdownTestUrlRequestCallback(testFramework.mCronetEngine);
    // Block callback when response starts to verify that shutdown fails
    // if there are active requests.
    callback.setAutoAdvance(false);
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    UrlRequest urlRequest = urlRequestBuilder.build();
    urlRequest.start();
    try {
      testFramework.mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }

    callback.waitForNextStep();
    assertEquals(ResponseStep.ON_RESPONSE_STARTED, callback.mResponseStep);
    try {
      testFramework.mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }
    callback.startNextRead(urlRequest);

    callback.waitForNextStep();
    assertEquals(ResponseStep.ON_READ_COMPLETED, callback.mResponseStep);
    try {
      testFramework.mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }

    // May not have read all the data, in theory. Just enable auto-advance
    // and finish the request.
    callback.setAutoAdvance(true);
    callback.startNextRead(urlRequest);
    callback.blockForDone();
    callback.blockForCallbackToComplete();
    callback.shutdownExecutor();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Test is running on main thread")
  public void testShutdownDuringInit() throws Exception {
    final ConditionVariable block = new ConditionVariable(false);

    // Post a task to main thread to block until shutdown is called to test
    // scenario when shutdown is called right after construction before
    // context is fully initialized on the main thread.
    Runnable blockingTask = new Runnable() {
      @Override
      public void run() {
        try {
          block.block();
        } catch (Exception e) {
          fail("Caught " + e.getMessage());
        }
      }
    };
    // Ensure that test is not running on the main thread.
    assertTrue(Looper.getMainLooper() != Looper.myLooper());
    new Handler(Looper.getMainLooper()).post(blockingTask);

    // Create new request context, but its initialization on the main thread
    // will be stuck behind blockingTask.
    final CronetUrlRequestContext cronetEngine =
        (CronetUrlRequestContext) new CronetEngine.Builder(getContext()).build();
    // Unblock the main thread, so context gets initialized and shutdown on
    // it.
    block.open();
    // Shutdown will wait for init to complete on main thread.
    cronetEngine.shutdown();
    // Verify that context is shutdown.
    assertTrue("Engine is shutdown", cronetEngine.hasShutdown());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Properly implement shutdown sequence")
  public void testInitAndShutdownOnMainThread() throws Exception {
    final ConditionVariable block = new ConditionVariable(false);

    // Post a task to main thread to init and shutdown on the main thread.
    Runnable blockingTask = new Runnable() {
      @Override
      public void run() {
        // Create new request context, loading the library.
        final CronetUrlRequestContext cronetEngine =
            (CronetUrlRequestContext) new CronetEngine.Builder(getContext()).build();
        // Shutdown right after init.
        cronetEngine.shutdown();
        // Verify that context is shutdown.
        assertTrue("Engine is shutdown", cronetEngine.hasShutdown());
        block.open();
      }
    };
    new Handler(Looper.getMainLooper()).post(blockingTask);
    // Wait for shutdown to complete on main thread.
    block.block();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // JavaCronetEngine doesn't support throwing on repeat shutdown()
  @Ignore("Properly implement shutdown sequence")
  public void testMultipleShutdown() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    try {
      testFramework.mCronetEngine.shutdown();
      testFramework.mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Engine is shut down.", e.getMessage());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  // TODO: Remove the annotation after fixing http://crbug.com/637972
  @OnlyRunNativeCronet
  @Ignore("Properly implement shutdown sequence")
  public void testShutdownAfterError() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    ShutdownTestUrlRequestCallback callback =
        new ShutdownTestUrlRequestCallback(testFramework.mCronetEngine);
    UrlRequest.Builder urlRequestBuilder = testFramework.mCronetEngine.newUrlRequestBuilder(
        MOCK_CRONET_TEST_FAILED_URL, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertTrue(callback.mOnErrorCalled);
    callback.blockForCallbackToComplete();
    callback.shutdownExecutor();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // JavaCronetEngine doesn't support throwing on shutdown()
  @Ignore("Shutdown not properly implemented")
  public void testShutdownAfterCancel() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    // Block callback when response starts to verify that shutdown fails
    // if there are active requests.
    callback.setAutoAdvance(false);
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    UrlRequest urlRequest = urlRequestBuilder.build();
    urlRequest.start();
    try {
      testFramework.mCronetEngine.shutdown();
      fail("Should throw an exception");
    } catch (Exception e) {
      assertEquals("Cannot shutdown with active requests.", e.getMessage());
    }
    callback.waitForNextStep();
    assertEquals(ResponseStep.ON_RESPONSE_STARTED, callback.mResponseStep);
    urlRequest.cancel();
    testFramework.mCronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // No netlogs for pure java impl
  @Ignore("Netlog not implemented")
  public void testNetLog() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File file = File.createTempFile("cronet", "json", directory);
    CronetEngine cronetEngine = new CronetEngine.Builder(context).build();
    // Start NetLog immediately after the request context is created to make
    // sure that the call won't crash the app even when the native request
    // context is not fully initialized. See crbug.com/470196.
    cronetEngine.startNetLogToFile(file.getPath(), false);

    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    cronetEngine.stopNetLog();
    assertTrue(file.exists());
    assertTrue(file.length() != 0);
    assertFalse(hasBytesInNetLog(file));
    assertTrue(file.delete());
    assertTrue(!file.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // No netlogs for pure java impl
  @Ignore("Netlog not implemented")
  public void testBoundedFileNetLog() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File netLogDir = new File(directory, "NetLog");
    assertFalse(netLogDir.exists());
    assertTrue(netLogDir.mkdir());
    File logFile = new File(netLogDir, "netlog.json");
    ExperimentalCronetEngine cronetEngine = new ExperimentalCronetEngine.Builder(context).build();
    // Start NetLog immediately after the request context is created to make
    // sure that the call won't crash the app even when the native request
    // context is not fully initialized. See crbug.com/470196.
    cronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);

    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    cronetEngine.stopNetLog();
    assertTrue(logFile.exists());
    assertTrue(logFile.length() != 0);
    assertFalse(hasBytesInNetLog(logFile));
    FileUtils.recursivelyDeleteFile(netLogDir, FileUtils.DELETE_ALL);
    assertFalse(netLogDir.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // No netlogs for pure java impl
  @Ignore("Netlog not implemented")
  // Tests that if stopNetLog is not explicitly called, CronetEngine.shutdown()
  // will take care of it. crbug.com/623701.
  public void testNoStopNetLog() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File file = File.createTempFile("cronet", "json", directory);
    CronetEngine cronetEngine = new CronetEngine.Builder(context).build();
    cronetEngine.startNetLogToFile(file.getPath(), false);

    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    // Shut down the engine without calling stopNetLog.
    cronetEngine.shutdown();
    assertTrue(file.exists());
    assertTrue(file.length() != 0);
    assertFalse(hasBytesInNetLog(file));
    assertTrue(file.delete());
    assertTrue(!file.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // No netlogs for pure java impl
  @Ignore("Netlog not implemented")
  // Tests that if stopNetLog is not explicitly called, CronetEngine.shutdown()
  // will take care of it. crbug.com/623701.
  public void testNoStopBoundedFileNetLog() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File netLogDir = new File(directory, "NetLog");
    assertFalse(netLogDir.exists());
    assertTrue(netLogDir.mkdir());
    File logFile = new File(netLogDir, "netlog.json");
    ExperimentalCronetEngine cronetEngine = new ExperimentalCronetEngine.Builder(context).build();
    cronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);

    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    // Shut down the engine without calling stopNetLog.
    cronetEngine.shutdown();
    assertTrue(logFile.exists());
    assertTrue(logFile.length() != 0);

    FileUtils.recursivelyDeleteFile(netLogDir, FileUtils.DELETE_ALL);
    assertFalse(netLogDir.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  // Tests that NetLog contains events emitted by all live CronetEngines.
  public void testNetLogContainEventsFromAllLiveEngines() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File file1 = File.createTempFile("cronet1", "json", directory);
    File file2 = File.createTempFile("cronet2", "json", directory);
    CronetEngine cronetEngine1 = new CronetEngine.Builder(context).build();
    CronetEngine cronetEngine2 = new CronetEngine.Builder(context).build();

    cronetEngine1.startNetLogToFile(file1.getPath(), false);
    cronetEngine2.startNetLogToFile(file2.getPath(), false);

    // Warm CronetEngine and make sure both CronetUrlRequestContexts are
    // initialized before testing the logs.
    makeRequestAndCheckStatus(cronetEngine1, mUrl, 200);
    makeRequestAndCheckStatus(cronetEngine2, mUrl, 200);

    // Use cronetEngine1 to make a request to mUrl404.
    makeRequestAndCheckStatus(cronetEngine1, mUrl404, 404);

    // Use cronetEngine2 to make a request to mUrl500.
    makeRequestAndCheckStatus(cronetEngine2, mUrl500, 500);

    cronetEngine1.stopNetLog();
    cronetEngine2.stopNetLog();
    assertTrue(file1.exists());
    assertTrue(file2.exists());
    // Make sure both files contain the two requests made separately using
    // different engines.
    assertTrue(containsStringInNetLog(file1, mUrl404));
    assertTrue(containsStringInNetLog(file1, mUrl500));
    assertTrue(containsStringInNetLog(file2, mUrl404));
    assertTrue(containsStringInNetLog(file2, mUrl500));
    assertTrue(file1.delete());
    assertTrue(file2.delete());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  // Tests that NetLog contains events emitted by all live CronetEngines.
  public void testBoundedFileNetLogContainEventsFromAllLiveEngines() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File netLogDir1 = new File(directory, "NetLog1");
    assertFalse(netLogDir1.exists());
    assertTrue(netLogDir1.mkdir());
    File netLogDir2 = new File(directory, "NetLog2");
    assertFalse(netLogDir2.exists());
    assertTrue(netLogDir2.mkdir());
    File logFile1 = new File(netLogDir1, "netlog.json");
    File logFile2 = new File(netLogDir2, "netlog.json");

    ExperimentalCronetEngine cronetEngine1 = new ExperimentalCronetEngine.Builder(context).build();
    ExperimentalCronetEngine cronetEngine2 = new ExperimentalCronetEngine.Builder(context).build();

    cronetEngine1.startNetLogToDisk(netLogDir1.getPath(), false, MAX_FILE_SIZE);
    cronetEngine2.startNetLogToDisk(netLogDir2.getPath(), false, MAX_FILE_SIZE);

    // Warm CronetEngine and make sure both CronetUrlRequestContexts are
    // initialized before testing the logs.
    makeRequestAndCheckStatus(cronetEngine1, mUrl, 200);
    makeRequestAndCheckStatus(cronetEngine2, mUrl, 200);

    // Use cronetEngine1 to make a request to mUrl404.
    makeRequestAndCheckStatus(cronetEngine1, mUrl404, 404);

    // Use cronetEngine2 to make a request to mUrl500.
    makeRequestAndCheckStatus(cronetEngine2, mUrl500, 500);

    cronetEngine1.stopNetLog();
    cronetEngine2.stopNetLog();

    assertTrue(logFile1.exists());
    assertTrue(logFile2.exists());
    assertTrue(logFile1.length() != 0);
    assertTrue(logFile2.length() != 0);

    // Make sure both files contain the two requests made separately using
    // different engines.
    assertTrue(containsStringInNetLog(logFile1, mUrl404));
    assertTrue(containsStringInNetLog(logFile1, mUrl500));
    assertTrue(containsStringInNetLog(logFile2, mUrl404));
    assertTrue(containsStringInNetLog(logFile2, mUrl500));

    FileUtils.recursivelyDeleteFile(netLogDir1, FileUtils.DELETE_ALL);
    assertFalse(netLogDir1.exists());
    FileUtils.recursivelyDeleteFile(netLogDir2, FileUtils.DELETE_ALL);
    assertFalse(netLogDir2.exists());
  }

  private CronetEngine createCronetEngineWithCache(int cacheType) {
    CronetEngine.Builder builder = new CronetEngine.Builder(getContext());
    if (cacheType == CronetEngine.Builder.HTTP_CACHE_DISK ||
        cacheType == CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP) {
      builder.setStoragePath(getTestStorage());
    }
    builder.enableHttpCache(cacheType, 100 * 1024);
    // Don't check the return value here, because startNativeTestServer() returns false when the
    // NativeTestServer is already running and this method needs to be called twice without
    // shutting down the NativeTestServer in between.
    NativeTestServer.startNativeTestServer(getContext());
    return builder.build();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  // Tests that if CronetEngine is shut down on the network thread, an appropriate exception
  // is thrown.
  public void testShutDownEngineOnNetworkThread() throws Exception {
    final CronetEngine cronetEngine =
        createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK);
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    // Make a request to a cacheable resource.
    checkRequestCaching(cronetEngine, url, false);

    final AtomicReference<Throwable> thrown = new AtomicReference<>();
    // Shut down the server.
    NativeTestServer.shutdownNativeTestServer();
    class CancelUrlRequestCallback extends TestUrlRequestCallback {
      @Override
      public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
        super.onResponseStarted(request, info);
        request.cancel();
        // Shut down CronetEngine immediately after request is destroyed.
        try {
          cronetEngine.shutdown();
        } catch (Exception e) {
          thrown.set(e);
        }
      }

      @Override
      public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
        // onSucceeded will not happen, because the request is canceled
        // after sending first read and the executor is single threaded.
        throw new RuntimeException("Unexpected");
      }

      @Override
      public void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error) {
        throw new RuntimeException("Unexpected");
      }
    }
    Executor directExecutor = new Executor() {
      @Override
      public void execute(Runnable command) {
        command.run();
      }
    };
    CancelUrlRequestCallback callback = new CancelUrlRequestCallback();
    callback.setAllowDirectExecutor(true);
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(url, callback, directExecutor);
    urlRequestBuilder.allowDirectExecutor();
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertTrue(thrown.get() instanceof RuntimeException);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  // Tests that if CronetEngine is shut down when reading from disk cache,
  // there isn't a crash. See crbug.com/486120.
  public void testShutDownEngineWhenReadingFromDiskCache() throws Exception {
    final CronetEngine cronetEngine =
        createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK);
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    // Make a request to a cacheable resource.
    checkRequestCaching(cronetEngine, url, false);

    // Shut down the server.
    NativeTestServer.shutdownNativeTestServer();
    class CancelUrlRequestCallback extends TestUrlRequestCallback {
      @Override
      public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
        super.onResponseStarted(request, info);
        request.cancel();
        // Shut down CronetEngine immediately after request is destroyed.
        cronetEngine.shutdown();
      }

      @Override
      public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
        // onSucceeded will not happen, because the request is canceled
        // after sending first read and the executor is single threaded.
        throw new RuntimeException("Unexpected");
      }

      @Override
      public void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error) {
        throw new RuntimeException("Unexpected");
      }
    }
    CancelUrlRequestCallback callback = new CancelUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(url, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    assertTrue(callback.mResponseInfo.wasCached());
    assertTrue(callback.mOnCanceledCalled);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testNetLogAfterShutdown() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    testFramework.mCronetEngine.shutdown();

    File directory = new File(PathUtils.getDataDirectory());
    File file = File.createTempFile("cronet", "json", directory);
    try {
      testFramework.mCronetEngine.startNetLogToFile(file.getPath(), false);
      fail("Should throw an exception.");
    } catch (Exception e) {
      assertEquals("Engine is shut down.", e.getMessage());
    }
    assertFalse(hasBytesInNetLog(file));
    assertTrue(file.delete());
    assertFalse(file.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testBoundedFileNetLogAfterShutdown() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    testFramework.mCronetEngine.shutdown();

    File directory = new File(PathUtils.getDataDirectory());
    File netLogDir = new File(directory, "NetLog");
    assertFalse(netLogDir.exists());
    assertTrue(netLogDir.mkdir());
    File logFile = new File(netLogDir, "netlog.json");
    try {
      testFramework.mCronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);
      fail("Should throw an exception.");
    } catch (Exception e) {
      assertEquals("Engine is shut down.", e.getMessage());
    }
    assertFalse(logFile.exists());
    FileUtils.recursivelyDeleteFile(netLogDir, FileUtils.DELETE_ALL);
    assertFalse(netLogDir.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testNetLogStartMultipleTimes() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    File directory = new File(PathUtils.getDataDirectory());
    File file = File.createTempFile("cronet", "json", directory);
    // Start NetLog multiple times.
    testFramework.mCronetEngine.startNetLogToFile(file.getPath(), false);
    testFramework.mCronetEngine.startNetLogToFile(file.getPath(), false);
    testFramework.mCronetEngine.startNetLogToFile(file.getPath(), false);
    testFramework.mCronetEngine.startNetLogToFile(file.getPath(), false);
    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    testFramework.mCronetEngine.stopNetLog();
    assertTrue(file.exists());
    assertTrue(file.length() != 0);
    assertFalse(hasBytesInNetLog(file));
    assertTrue(file.delete());
    assertFalse(file.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testBoundedFileNetLogStartMultipleTimes() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    File directory = new File(PathUtils.getDataDirectory());
    File netLogDir = new File(directory, "NetLog");
    assertFalse(netLogDir.exists());
    assertTrue(netLogDir.mkdir());
    File logFile = new File(netLogDir, "netlog.json");
    // Start NetLog multiple times. This should be equivalent to starting NetLog
    // once. Each subsequent start (without calling stopNetLog) should be a no-op.
    testFramework.mCronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);
    testFramework.mCronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);
    testFramework.mCronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);
    testFramework.mCronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);
    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    testFramework.mCronetEngine.stopNetLog();
    assertTrue(logFile.exists());
    assertTrue(logFile.length() != 0);
    assertFalse(hasBytesInNetLog(logFile));
    FileUtils.recursivelyDeleteFile(netLogDir, FileUtils.DELETE_ALL);
    assertFalse(netLogDir.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testNetLogStopMultipleTimes() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    File directory = new File(PathUtils.getDataDirectory());
    File file = File.createTempFile("cronet", "json", directory);
    testFramework.mCronetEngine.startNetLogToFile(file.getPath(), false);
    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    // Stop NetLog multiple times.
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    assertTrue(file.exists());
    assertTrue(file.length() != 0);
    assertFalse(hasBytesInNetLog(file));
    assertTrue(file.delete());
    assertFalse(file.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testBoundedFileNetLogStopMultipleTimes() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();
    File directory = new File(PathUtils.getDataDirectory());
    File netLogDir = new File(directory, "NetLog");
    assertFalse(netLogDir.exists());
    assertTrue(netLogDir.mkdir());
    File logFile = new File(netLogDir, "netlog.json");
    testFramework.mCronetEngine.startNetLogToDisk(netLogDir.getPath(), false, MAX_FILE_SIZE);
    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    // Stop NetLog multiple times. This should be equivalent to stopping NetLog once.
    // Each subsequent stop (without calling startNetLogToDisk first) should be a no-op.
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    testFramework.mCronetEngine.stopNetLog();
    assertTrue(logFile.exists());
    assertTrue(logFile.length() != 0);
    assertFalse(hasBytesInNetLog(logFile));
    FileUtils.recursivelyDeleteFile(netLogDir, FileUtils.DELETE_ALL);
    assertFalse(netLogDir.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testNetLogWithBytes() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File file = File.createTempFile("cronet", "json", directory);
    CronetEngine cronetEngine = new CronetEngine.Builder(context).build();
    // Start NetLog with logAll as true.
    cronetEngine.startNetLogToFile(file.getPath(), true);
    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    cronetEngine.stopNetLog();
    assertTrue(file.exists());
    assertTrue(file.length() != 0);
    assertTrue(hasBytesInNetLog(file));
    assertTrue(file.delete());
    assertFalse(file.exists());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Netlog not implemented")
  public void testBoundedFileNetLogWithBytes() throws Exception {
    Context context = getContext();
    File directory = new File(PathUtils.getDataDirectory());
    File netLogDir = new File(directory, "NetLog");
    assertFalse(netLogDir.exists());
    assertTrue(netLogDir.mkdir());
    File logFile = new File(netLogDir, "netlog.json");
    ExperimentalCronetEngine cronetEngine = new ExperimentalCronetEngine.Builder(context).build();
    // Start NetLog with logAll as true.
    cronetEngine.startNetLogToDisk(netLogDir.getPath(), true, MAX_FILE_SIZE);
    // Start a request.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    cronetEngine.stopNetLog();

    assertTrue(logFile.exists());
    assertTrue(logFile.length() != 0);
    assertTrue(hasBytesInNetLog(logFile));
    FileUtils.recursivelyDeleteFile(netLogDir, FileUtils.DELETE_ALL);
    assertFalse(netLogDir.exists());
  }

  private boolean hasBytesInNetLog(File logFile) throws Exception {
    return containsStringInNetLog(logFile, "\"bytes\"");
  }

  private boolean containsStringInNetLog(File logFile, String content) throws Exception {
    BufferedReader logReader = new BufferedReader(new FileReader(logFile));
    try {
      String logLine;
      while ((logLine = logReader.readLine()) != null) {
        if (logLine.contains(content)) {
          return true;
        }
      }
      return false;
    } finally {
      logReader.close();
    }
  }

  /**
   * Helper method to make a request to {@code url}, wait for it to
   * complete, and check that the status code is the same as {@code expectedStatusCode}.
   */
  private void makeRequestAndCheckStatus(CronetEngine engine, String url, int expectedStatusCode) {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest request = engine.newUrlRequestBuilder(url, callback, callback.getExecutor()).build();
    request.start();
    callback.blockForDone();
    assertEquals(expectedStatusCode, callback.mResponseInfo.getHttpStatusCode());
  }

  private void checkRequestCaching(CronetEngine engine, String url, boolean expectCached) {
    checkRequestCaching(engine, url, expectCached, false);
  }

  private void checkRequestCaching(CronetEngine engine, String url, boolean expectCached,
                                   boolean disableCache) {
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        engine.newUrlRequestBuilder(url, callback, callback.getExecutor());
    if (disableCache) {
      urlRequestBuilder.disableCache();
    }
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertEquals(expectCached, callback.mResponseInfo.wasCached());
    assertEquals("this is a cacheable file\n", callback.mResponseAsString);
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  public void testEnableHttpCacheDisabled() throws Exception {
    CronetEngine cronetEngine =
        createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISABLED);
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, false);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  public void testEnableHttpCacheInMemory() throws Exception {
    CronetEngine cronetEngine =
        createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_IN_MEMORY);
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, true);
    NativeTestServer.shutdownNativeTestServer();
    checkRequestCaching(cronetEngine, url, true);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  public void testEnableHttpCacheDisk() throws Exception {
    CronetEngine cronetEngine = createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK);
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, true);
    NativeTestServer.shutdownNativeTestServer();
    checkRequestCaching(cronetEngine, url, true);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  public void testNoConcurrentDiskUsage() throws Exception {
    CronetEngine cronetEngine = createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK);
    try {
      createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK);
      fail();
    } catch (IllegalStateException e) {
      assertEquals("Disk cache storage path already in use", e.getMessage());
    }
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, true);
    NativeTestServer.shutdownNativeTestServer();
    checkRequestCaching(cronetEngine, url, true);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  public void testEnableHttpCacheDiskNoHttp() throws Exception {
    CronetEngine cronetEngine =
        createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP);
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, false);

    // Make a new CronetEngine and try again to make sure the response didn't get cached on the
    // first request. See https://crbug.com/743232.
    cronetEngine.shutdown();
    cronetEngine = createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP);
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, false);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  public void testDisableCache() throws Exception {
    CronetEngine cronetEngine = createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK);
    String url = NativeTestServer.getFileURL("/cacheable.txt");

    // When cache is disabled, making a request does not write to the cache.
    checkRequestCaching(cronetEngine, url, false, true /** disable cache */);
    checkRequestCaching(cronetEngine, url, false);

    // When cache is enabled, the second request is cached.
    checkRequestCaching(cronetEngine, url, false, true /** disable cache */);
    checkRequestCaching(cronetEngine, url, true);

    // Shut down the server, next request should have a cached response.
    NativeTestServer.shutdownNativeTestServer();
    checkRequestCaching(cronetEngine, url, true);

    // Cache is disabled after server is shut down, request should fail.
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(url, callback, callback.getExecutor());
    urlRequestBuilder.disableCache();
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertNotNull(callback.mError);
    assertContains("Exception in CronetUrlRequest: net::ERR_CONNECTION_REFUSED",
                   callback.mError.getMessage());
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Caching not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1578")
  public void testEnableHttpCacheDiskNewEngine() throws Exception {
    CronetEngine cronetEngine = createCronetEngineWithCache(CronetEngine.Builder.HTTP_CACHE_DISK);
    String url = NativeTestServer.getFileURL("/cacheable.txt");
    checkRequestCaching(cronetEngine, url, false);
    checkRequestCaching(cronetEngine, url, true);
    NativeTestServer.shutdownNativeTestServer();
    checkRequestCaching(cronetEngine, url, true);

    // Shutdown original context and create another that uses the same cache.
    cronetEngine.shutdown();
    cronetEngine = mTestRule.enableDiskCache(new CronetEngine.Builder(getContext())).build();
    checkRequestCaching(cronetEngine, url, true);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testInitEngineAndStartRequest() {
    // Immediately make a request after initializing the engine.
    CronetEngine cronetEngine = new CronetEngine.Builder(getContext()).build();
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testInitEngineStartTwoRequests() throws Exception {
    // Make two requests after initializing the context.
    CronetEngine cronetEngine = new CronetEngine.Builder(getContext()).build();
    int[] statusCodes = {0, 0};
    String[] urls = {mUrl, mUrl404};
    for (int i = 0; i < 2; i++) {
      TestUrlRequestCallback callback = new TestUrlRequestCallback();
      UrlRequest.Builder urlRequestBuilder =
          cronetEngine.newUrlRequestBuilder(urls[i], callback, callback.getExecutor());
      urlRequestBuilder.build().start();
      callback.blockForDone();
      statusCodes[i] = callback.mResponseInfo.getHttpStatusCode();
    }
    assertEquals(200, statusCodes[0]);
    assertEquals(404, statusCodes[1]);
    cronetEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("This causes a deadlock")
  public void testInitTwoEnginesSimultaneously() throws Exception {
    // Threads will block on runBlocker to ensure simultaneous execution.
    ConditionVariable runBlocker = new ConditionVariable(false);
    RequestThread thread1 = new RequestThread(mUrl, runBlocker);
    RequestThread thread2 = new RequestThread(mUrl404, runBlocker);

    thread1.start();
    thread2.start();
    runBlocker.open();
    thread1.join();
    thread2.join();
    assertEquals(200, thread1.mCallback.mResponseInfo.getHttpStatusCode());
    assertEquals(404, thread2.mCallback.mResponseInfo.getHttpStatusCode());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testInitTwoEnginesInSequence() throws Exception {
    ConditionVariable runBlocker = new ConditionVariable(true);
    RequestThread thread1 = new RequestThread(mUrl, runBlocker);
    RequestThread thread2 = new RequestThread(mUrl404, runBlocker);

    thread1.start();
    thread1.join();
    thread2.start();
    thread2.join();
    assertEquals(200, thread1.mCallback.mResponseInfo.getHttpStatusCode());
    assertEquals(404, thread2.mCallback.mResponseInfo.getHttpStatusCode());
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("This causes a crash")
  public void testInitDifferentEngines() throws Exception {
    // Test that concurrently instantiating Cronet context's upon various
    // different versions of the same Android Context does not cause crashes
    // like crbug.com/453845
    CronetEngine firstEngine = new CronetEngine.Builder(getContext()).build();
    CronetEngine secondEngine =
        new CronetEngine.Builder(getContext().getApplicationContext()).build();
    CronetEngine thirdEngine = new CronetEngine.Builder(new ContextWrapper(getContext())).build();
    firstEngine.shutdown();
    secondEngine.shutdown();
    thirdEngine.shutdown();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet // Java engine doesn't produce metrics
  @Ignore("Metrics not implemented yet: https://github.com/envoyproxy/envoy-mobile/issues/1520")
  public void testGetGlobalMetricsDeltas() throws Exception {
    final CronetTestFramework testFramework = mTestRule.startCronetTestFramework();

    byte[] delta1 = testFramework.mCronetEngine.getGlobalMetricsDeltas();

    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    UrlRequest.Builder builder =
        testFramework.mCronetEngine.newUrlRequestBuilder(mUrl, callback, callback.getExecutor());
    builder.build().start();
    callback.blockForDone();
    // Fetch deltas on a different thread the second time to make sure this is permitted.
    // See crbug.com/719448
    FutureTask<byte[]> task = new FutureTask<>(new Callable<byte[]>() {
      @Override
      public byte[] call() {
        return testFramework.mCronetEngine.getGlobalMetricsDeltas();
      }
    });
    new Thread(task).start();
    byte[] delta2 = task.get();
    assertTrue(delta2.length != 0);
    assertFalse(Arrays.equals(delta1, delta2));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Look into another way to verify this setup")
  public void testCronetEngineBuilderConfig() throws Exception {
    // This is to prompt load of native library.
    mTestRule.startCronetTestFramework();
    // Verify CronetEngine.Builder config is passed down accurately to native code.
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    builder.enableHttp2(false);
    builder.enableQuic(true);
    builder.addQuicHint("example.com", 12, 34);
    builder.enableHttpCache(HTTP_CACHE_IN_MEMORY, 54321);
    builder.setUserAgent("efgh");
    builder.setExperimentalOptions("ijkl");
    builder.setStoragePath(getTestStorage());
    builder.enablePublicKeyPinningBypassForLocalTrustAnchors(false);
    // TODO(colibie)
    // nativeVerifyUrlRequestContextConfig(
    //         CronetUrlRequestContext.createNativeUrlRequestContextConfig(
    //                 (CronetEngineBuilderImpl) builder.mBuilderDelegate),
    //         getTestStorage());
  }

  // Verifies that CronetEngine.Builder config from testCronetEngineBuilderConfig() is properly
  // translated to a native UrlRequestContextConfig.
  private static native void nativeVerifyUrlRequestContextConfig(long config, String storagePath);

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Look into another way to verify this setup")
  public void testCronetEngineQuicOffConfig() throws Exception {
    // This is to prompt load of native library.
    mTestRule.startCronetTestFramework();
    // Verify CronetEngine.Builder config is passed down accurately to native code.
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    builder.enableHttp2(false);
    // QUIC is on by default. Disabling it here to make sure the built config can correctly
    // reflect the change.
    builder.enableQuic(false);
    builder.enableHttpCache(HTTP_CACHE_IN_MEMORY, 54321);
    builder.setExperimentalOptions("ijkl");
    builder.setUserAgent("efgh");
    builder.setStoragePath(getTestStorage());
    builder.enablePublicKeyPinningBypassForLocalTrustAnchors(false);
    // nativeVerifyUrlRequestContextQuicOffConfig(
    //         CronetUrlRequestContext.createNativeUrlRequestContextConfig(
    //                 (CronetEngineBuilderImpl) builder.mBuilderDelegate),
    //         getTestStorage());
  }

  // Verifies that CronetEngine.Builder config from testCronetEngineQuicOffConfig() is properly
  // translated to a native UrlRequestContextConfig and QUIC is turned off.
  private static native void nativeVerifyUrlRequestContextQuicOffConfig(long config,
                                                                        String storagePath);

  private static class TestBadLibraryLoader extends CronetEngine.Builder.LibraryLoader {
    private boolean mWasCalled;

    @Override
    public void loadLibrary(String libName) {
      // Report that this method was called, but don't load the library
      mWasCalled = true;
    }

    boolean wasCalled() { return mWasCalled; }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Library Loader not needed")
  public void testSetLibraryLoaderIsEnforcedByDefaultEmbeddedProvider() throws Exception {
    CronetEngine.Builder builder = new CronetEngine.Builder(getContext());
    TestBadLibraryLoader loader = new TestBadLibraryLoader();
    builder.setLibraryLoader(loader);
    try {
      builder.build();
      fail("Native library should not be loaded");
    } catch (UnsatisfiedLinkError e) {
      assertTrue(loader.wasCalled());
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  @Ignore("Library Loader not needed")
  public void testSetLibraryLoaderIsIgnoredInNativeCronetEngineBuilderImpl() throws Exception {
    CronetEngine.Builder builder =
        new CronetEngine.Builder(new NativeCronetEngineBuilderImpl(getContext()));
    TestBadLibraryLoader loader = new TestBadLibraryLoader();
    builder.setLibraryLoader(loader);
    CronetEngine engine = builder.build();
    assertNotNull(engine);
    assertFalse(loader.wasCalled());
  }

  // Creates a CronetEngine on another thread and then one on the main thread. This shouldn't
  // crash.
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("This times out")
  public void testThreadedStartup() throws Exception {
    final ConditionVariable otherThreadDone = new ConditionVariable();
    final ConditionVariable uiThreadDone = new ConditionVariable();
    new Handler(Looper.getMainLooper()).post(new Runnable() {
      @Override
      public void run() {
        final ExperimentalCronetEngine.Builder builder =
            new ExperimentalCronetEngine.Builder(getContext());
        new Thread() {
          @Override
          public void run() {
            CronetEngine cronetEngine = builder.build();
            otherThreadDone.open();
            cronetEngine.shutdown();
          }
        }.start();
        otherThreadDone.block();
        builder.build().shutdown();
        uiThreadDone.open();
      }
    });
    assertTrue(uiThreadDone.block(1000));
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @Ignore("Experimental options not implemented yet")
  public void testHostResolverRules() throws Exception {
    String resolverTestHostname = "some-weird-hostname";
    URL testUrl = new URL(mUrl);
    ExperimentalCronetEngine.Builder cronetEngineBuilder =
        new ExperimentalCronetEngine.Builder(getContext());
    JSONObject hostResolverRules = new JSONObject().put(
        "host_resolver_rules", "MAP " + resolverTestHostname + " " + testUrl.getHost());
    JSONObject experimentalOptions = new JSONObject().put("HostResolverRules", hostResolverRules);
    cronetEngineBuilder.setExperimentalOptions(experimentalOptions.toString());

    final CronetEngine cronetEngine = cronetEngineBuilder.build();
    TestUrlRequestCallback callback = new TestUrlRequestCallback();
    URL requestUrl = new URL("http", resolverTestHostname, testUrl.getPort(), testUrl.getFile());
    UrlRequest.Builder urlRequestBuilder =
        cronetEngine.newUrlRequestBuilder(requestUrl.toString(), callback, callback.getExecutor());
    urlRequestBuilder.build().start();
    callback.blockForDone();
    assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
  }

  /**
   * Runs {@code r} on {@code engine}'s network thread.
   */
  private static void postToNetworkThread(final CronetEngine engine, final Runnable r) {
    // Works by requesting an invalid URL which results in onFailed() being called, which is
    // done through a direct executor which causes onFailed to be run on the network thread.
    Executor directExecutor = new Executor() {
      @Override
      public void execute(Runnable runnable) {
        runnable.run();
      }
    };
    UrlRequest.Callback callback = new UrlRequest.Callback() {
      @Override
      public void onRedirectReceived(UrlRequest request, UrlResponseInfo responseInfo,
                                     String newLocationUrl) {}
      @Override
      public void onResponseStarted(UrlRequest request, UrlResponseInfo responseInfo) {}
      @Override
      public void onReadCompleted(UrlRequest request, UrlResponseInfo responseInfo,
                                  ByteBuffer byteBuffer) {}
      @Override
      public void onSucceeded(UrlRequest request, UrlResponseInfo responseInfo) {}

      @Override
      public void onFailed(UrlRequest request, UrlResponseInfo responseInfo,
                           CronetException error) {
        r.run();
      }
    };
    engine.newUrlRequestBuilder("http://invalid", callback, directExecutor).build().start();
  }

  /**
   * @return the thread priority of {@code engine}'s network thread.
   */
  private int getThreadPriority(CronetEngine engine) throws Exception {
    FutureTask<Integer> task = new FutureTask<>(new Callable<Integer>() {
      @Override
      public Integer call() {
        return Process.getThreadPriority(Process.myTid());
      }
    });
    postToNetworkThread(engine, task);
    return task.get();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  @RequiresMinApi(6) // setThreadPriority added in API 6: crrev.com/472449
  public void testCronetEngineThreadPriority() throws Exception {
    ExperimentalCronetEngine.Builder builder = new ExperimentalCronetEngine.Builder(getContext());
    // Try out of bounds thread priorities.
    try {
      builder.setThreadPriority(-21);
      fail();
    } catch (IllegalArgumentException e) {
      assertEquals("Thread priority invalid", e.getMessage());
    }
    try {
      builder.setThreadPriority(20);
      fail();
    } catch (IllegalArgumentException e) {
      assertEquals("Thread priority invalid", e.getMessage());
    }
    // Test that valid thread priority range (-20..19) is working.
    for (int threadPriority = -20; threadPriority < 20; threadPriority++) {
      builder.setThreadPriority(threadPriority);
      CronetEngine engine = builder.build();
      assertEquals(threadPriority, getThreadPriority(engine));
      engine.shutdown();
    }
  }
}
