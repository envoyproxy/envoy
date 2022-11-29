package org.chromium.net.testing;

import android.os.ConditionVariable;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.concurrent.Executor;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UploadDataSink;

/**
 * An UploadDataProvider that allows tests to invoke {@code onReadSucceeded}
 * and {@code onRewindSucceeded} on the UploadDataSink directly.
 * Chunked mode is not supported here, since the main interest is to test
 * different order of init/read/rewind calls.
 */
public final class TestDrivenDataProvider extends UploadDataProvider {
  private final Executor mExecutor;
  private final List<byte[]> mReads;
  private final ConditionVariable mWaitForReadRequest = new ConditionVariable();
  private final ConditionVariable mWaitForRewindRequest = new ConditionVariable();
  // Lock used to synchronize access to mReadPending and mRewindPending.
  private final Object mLock = new Object();

  private int mNextRead;

  // Only accessible when holding mLock.

  private boolean mReadPending;
  private boolean mRewindPending;
  private int mNumRewindCalls;
  private int mNumReadCalls;

  /**
   * Constructor.
   * @param Executor executor. Executor to run callbacks of UploadDataSink.
   * @param List<byte[]> reads. Results to be returned by successful read
   *            requests. Returned bytes must all fit within the read buffer
   *            provided by Cronet. After a rewind, if there is one, all reads
   *            will be repeated.
   */
  public TestDrivenDataProvider(Executor executor, List<byte[]> reads) {
    mExecutor = executor;
    mReads = reads;
  }

  // Called by UploadDataSink on the main thread.
  @Override
  public long getLength() {
    long length = 0;
    for (byte[] read : mReads) {
      length += read.length;
    }
    return length;
  }

  // Called by UploadDataSink on the executor thread.
  @Override
  public void read(final UploadDataSink uploadDataSink, final ByteBuffer byteBuffer)
      throws IOException {
    synchronized (mLock) {
      ++mNumReadCalls;
      assertIdle();

      mReadPending = true;
      if (mNextRead != mReads.size()) {
        if ((byteBuffer.limit() - byteBuffer.position()) < mReads.get(mNextRead).length) {
          throw new IllegalStateException("Read buffer smaller than expected.");
        }
        byteBuffer.put(mReads.get(mNextRead));
        ++mNextRead;
      } else {
        throw new IllegalStateException("Too many reads: " + mNextRead);
      }
      mWaitForReadRequest.open();
    }
  }

  // Called by UploadDataSink on the executor thread.
  @Override
  public void rewind(final UploadDataSink uploadDataSink) throws IOException {
    synchronized (mLock) {
      ++mNumRewindCalls;
      assertIdle();

      if (mNextRead == 0) {
        // Should never try and rewind when rewinding does nothing.
        throw new IllegalStateException("Unexpected rewind when already at beginning");
      }
      mRewindPending = true;
      mNextRead = 0;
      mWaitForRewindRequest.open();
    }
  }

  // Called by test fixture on the main thread.
  public void onReadSucceeded(final UploadDataSink uploadDataSink) {
    Runnable completeRunnable = new Runnable() {
      @Override
      public void run() {
        synchronized (mLock) {
          if (!mReadPending) {
            throw new IllegalStateException("No read pending.");
          }
          mReadPending = false;
          uploadDataSink.onReadSucceeded(false);
        }
      }
    };
    mExecutor.execute(completeRunnable);
  }

  // Called by test fixture on the main thread.
  public void onRewindSucceeded(final UploadDataSink uploadDataSink) {
    Runnable completeRunnable = new Runnable() {
      @Override
      public void run() {
        synchronized (mLock) {
          if (!mRewindPending) {
            throw new IllegalStateException("No rewind pending.");
          }
          mRewindPending = false;
          uploadDataSink.onRewindSucceeded();
        }
      }
    };
    mExecutor.execute(completeRunnable);
  }

  // Called by test fixture on the main thread.
  public int getNumReadCalls() {
    synchronized (mLock) { return mNumReadCalls; }
  }

  // Called by test fixture on the main thread.
  public int getNumRewindCalls() {
    synchronized (mLock) { return mNumRewindCalls; }
  }

  // Called by test fixture on the main thread.
  public void waitForReadRequest() { mWaitForReadRequest.block(); }

  // Called by test fixture on the main thread.
  public void resetWaitForReadRequest() { mWaitForReadRequest.close(); }

  // Called by test fixture on the main thread.
  public void waitForRewindRequest() { mWaitForRewindRequest.block(); }

  // Called by test fixture on the main thread.
  public void assertReadNotPending() {
    synchronized (mLock) {
      if (mReadPending) {
        throw new IllegalStateException("Read is pending.");
      }
    }
  }

  // Called by test fixture on the main thread.
  public void assertRewindNotPending() {
    synchronized (mLock) {
      if (mRewindPending) {
        throw new IllegalStateException("Rewind is pending.");
      }
    }
  }

  /**
   * Helper method to ensure no read or rewind is in progress.
   */
  private void assertIdle() {
    assertReadNotPending();
    assertRewindNotPending();
  }
}
