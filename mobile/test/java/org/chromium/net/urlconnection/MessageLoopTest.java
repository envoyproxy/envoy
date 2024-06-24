package org.chromium.net.urlconnection;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import androidx.test.filters.SmallTest;
import java.io.IOException;
import java.io.InterruptedIOException;
import java.net.SocketTimeoutException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadFactory;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.Feature;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

/**
 * Tests the MessageLoop implementation.
 */
@RunWith(RobolectricTestRunner.class)
public class MessageLoopTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private Thread mTestThread;
  private final ExecutorService mExecutorService =
      Executors.newSingleThreadExecutor(new ExecutorThreadFactory());
  private class ExecutorThreadFactory implements ThreadFactory {
    @Override
    public Thread newThread(Runnable r) {
      mTestThread = new Thread(r);
      return mTestThread;
    }
  }
  private boolean mFailed;

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testInterrupt() throws Exception {
    final CronvoyMessageLoop loop = new CronvoyMessageLoop();
    assertFalse(loop.isRunning());
    Future future = mExecutorService.submit(new Runnable() {
      @Override
      public void run() {
        try {
          loop.loop();
          mFailed = true;
        } catch (IOException e) {
          // Expected interrupt.
        }
      }
    });
    Thread.sleep(1000);
    assertTrue(loop.isRunning());
    assertFalse(loop.hasLoopFailed());
    mTestThread.interrupt();
    future.get();
    assertFalse(loop.isRunning());
    assertTrue(loop.hasLoopFailed());
    assertFalse(mFailed);
    // Re-spinning the message loop is not allowed after interrupt.
    mExecutorService
        .submit(new Runnable() {
          @Override
          public void run() {
            try {
              loop.loop();
              fail();
            } catch (Exception e) {
              if (!(e instanceof InterruptedIOException)) {
                fail();
              }
            }
          }
        })
        .get();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testTaskFailed() throws Exception {
    final CronvoyMessageLoop loop = new CronvoyMessageLoop();
    assertFalse(loop.isRunning());
    Future future = mExecutorService.submit(new Runnable() {
      @Override
      public void run() {
        try {
          loop.loop();
          mFailed = true;
        } catch (Exception e) {
          if (!(e instanceof NullPointerException)) {
            mFailed = true;
          }
        }
      }
    });
    Runnable failedTask = new Runnable() {
      @Override
      public void run() {
        throw new NullPointerException();
      }
    };
    Thread.sleep(1000);
    assertTrue(loop.isRunning());
    assertFalse(loop.hasLoopFailed());
    loop.execute(failedTask);
    future.get();
    assertFalse(loop.isRunning());
    assertTrue(loop.hasLoopFailed());
    assertFalse(mFailed);
    // Re-spinning the message loop is not allowed after exception.
    mExecutorService
        .submit(new Runnable() {
          @Override
          public void run() {
            try {
              loop.loop();
              fail();
            } catch (Exception e) {
              if (!(e instanceof NullPointerException)) {
                fail();
              }
            }
          }
        })
        .get();
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testLoopWithTimeout() throws Exception {
    final CronvoyMessageLoop loop = new CronvoyMessageLoop();
    assertFalse(loop.isRunning());
    // The MessageLoop queue is empty. Use a timeout of 100ms to check that
    // it doesn't block forever.
    try {
      loop.loop(100);
      fail();
    } catch (SocketTimeoutException e) {
      // Expected.
    }
    assertFalse(loop.isRunning());
  }
}
