package org.chromium.net.testing;

import android.os.ConditionVariable;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.ClosedChannelException;
import java.util.ArrayList;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicBoolean;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UploadDataSink;

/**
 * An UploadDataProvider implementation used in tests.
 */
public class TestUploadDataProvider extends UploadDataProvider {
  // Indicates whether all success callbacks are synchronous or asynchronous.
  // Doesn't apply to errors.
  public enum SuccessCallbackMode { SYNC, ASYNC }

  // Indicates whether failures should throw exceptions, invoke callbacks
  // synchronously, or invoke callback asynchronously.
  public enum FailMode { NONE, THROWN, CALLBACK_SYNC, CALLBACK_ASYNC }

  private ArrayList<byte[]> mReads = new ArrayList<byte[]>();
  private final SuccessCallbackMode mSuccessCallbackMode;
  private final Executor mExecutor;

  private boolean mChunked;

  // Index of read to fail on.
  private int mReadFailIndex = -1;
  // Indicates how to fail on a read.
  private FailMode mReadFailMode = FailMode.NONE;

  private FailMode mRewindFailMode = FailMode.NONE;

  private FailMode mLengthFailMode = FailMode.NONE;

  private int mNumReadCalls;
  private int mNumRewindCalls;

  private int mNextRead;
  private boolean mStarted;
  private boolean mReadPending;
  private boolean mRewindPending;
  // Used to ensure there are no read/rewind requests after a failure.
  private boolean mFailed;

  private AtomicBoolean mClosed = new AtomicBoolean(false);
  private ConditionVariable mAwaitingClose = new ConditionVariable(false);

  public TestUploadDataProvider(SuccessCallbackMode successCallbackMode, final Executor executor) {
    mSuccessCallbackMode = successCallbackMode;
    mExecutor = executor;
  }

  // Adds the result to be returned by a successful read request. The
  // returned bytes must all fit within the read buffer provided by Cronet.
  // After a rewind, if there is one, all reads will be repeated.
  public void addRead(byte[] read) {
    if (mStarted) {
      throw new IllegalStateException("Adding bytes after read");
    }
    mReads.add(read);
  }

  public void setReadFailure(int readFailIndex, FailMode readFailMode) {
    mReadFailIndex = readFailIndex;
    mReadFailMode = readFailMode;
  }

  public void setLengthFailure() { mLengthFailMode = FailMode.THROWN; }

  public void setRewindFailure(FailMode rewindFailMode) { mRewindFailMode = rewindFailMode; }

  public void setChunked(boolean chunked) { mChunked = chunked; }

  public int getNumReadCalls() { return mNumReadCalls; }

  public int getNumRewindCalls() { return mNumRewindCalls; }

  /**
   * Returns the cumulative length of all data added by calls to addRead.
   */
  @Override
  public long getLength() throws IOException {
    if (mClosed.get()) {
      throw new ClosedChannelException();
    }
    if (mLengthFailMode == FailMode.THROWN) {
      throw new IllegalStateException("Sync length failure");
    }
    return getUploadedLength();
  }

  public long getUploadedLength() {
    if (mChunked) {
      return -1;
    }
    long length = 0;
    for (byte[] read : mReads) {
      length += read.length;
    }
    return length;
  }

  @Override
  public void read(final UploadDataSink uploadDataSink, final ByteBuffer byteBuffer)
      throws IOException {
    int currentReadCall = mNumReadCalls;
    ++mNumReadCalls;
    if (mClosed.get()) {
      throw new ClosedChannelException();
    }
    assertIdle();

    if (maybeFailRead(currentReadCall, uploadDataSink)) {
      mFailed = true;
      return;
    }

    mReadPending = true;
    mStarted = true;

    final boolean finalChunk = (mChunked && mNextRead == mReads.size() - 1);
    if (mNextRead < mReads.size()) {
      if ((byteBuffer.limit() - byteBuffer.position()) < mReads.get(mNextRead).length) {
        throw new IllegalStateException("Read buffer smaller than expected.");
      }
      byteBuffer.put(mReads.get(mNextRead));
      ++mNextRead;
    } else {
      throw new IllegalStateException("Too many reads: " + mNextRead);
    }

    Runnable completeRunnable = new Runnable() {
      @Override
      public void run() {
        mReadPending = false;
        uploadDataSink.onReadSucceeded(finalChunk);
      }
    };
    if (mSuccessCallbackMode == SuccessCallbackMode.SYNC) {
      completeRunnable.run();
    } else {
      mExecutor.execute(completeRunnable);
    }
  }

  @Override
  public void rewind(final UploadDataSink uploadDataSink) throws IOException {
    ++mNumRewindCalls;
    if (mClosed.get()) {
      throw new ClosedChannelException();
    }
    assertIdle();

    if (maybeFailRewind(uploadDataSink)) {
      mFailed = true;
      return;
    }

    if (mNextRead == 0) {
      // Should never try and rewind when rewinding does nothing.
      throw new IllegalStateException("Unexpected rewind when already at beginning");
    }

    mRewindPending = true;
    mNextRead = 0;

    Runnable completeRunnable = new Runnable() {
      @Override
      public void run() {
        mRewindPending = false;
        uploadDataSink.onRewindSucceeded();
      }
    };
    if (mSuccessCallbackMode == SuccessCallbackMode.SYNC) {
      completeRunnable.run();
    } else {
      mExecutor.execute(completeRunnable);
    }
  }

  private void assertIdle() {
    if (mReadPending) {
      throw new IllegalStateException("Unexpected operation during read");
    }
    if (mRewindPending) {
      throw new IllegalStateException("Unexpected operation during rewind");
    }
    if (mFailed) {
      throw new IllegalStateException("Unexpected operation after failure");
    }
  }

  private boolean maybeFailRead(int readIndex, final UploadDataSink uploadDataSink) {
    if (readIndex != mReadFailIndex)
      return false;

    switch (mReadFailMode) {
    case THROWN:
      throw new IllegalStateException("Thrown read failure");
    case CALLBACK_SYNC:
      uploadDataSink.onReadError(new IllegalStateException("Sync read failure"));
      return true;
    case CALLBACK_ASYNC:
      Runnable errorRunnable = new Runnable() {
        @Override
        public void run() {
          uploadDataSink.onReadError(new IllegalStateException("Async read failure"));
        }
      };
      mExecutor.execute(errorRunnable);
      return true;
    default:
      return false;
    }
  }

  private boolean maybeFailRewind(final UploadDataSink uploadDataSink) {
    switch (mRewindFailMode) {
    case THROWN:
      throw new IllegalStateException("Thrown rewind failure");
    case CALLBACK_SYNC:
      uploadDataSink.onRewindError(new IllegalStateException("Sync rewind failure"));
      return true;
    case CALLBACK_ASYNC:
      Runnable errorRunnable = new Runnable() {
        @Override
        public void run() {
          uploadDataSink.onRewindError(new IllegalStateException("Async rewind failure"));
        }
      };
      mExecutor.execute(errorRunnable);
      return true;
    default:
      return false;
    }
  }

  @Override
  public void close() throws IOException {
    if (!mClosed.compareAndSet(false, true)) {
      throw new AssertionError("Closed twice");
    }
    mAwaitingClose.open();
  }

  public void assertClosed() {
    mAwaitingClose.block(5000);
    if (!mClosed.get()) {
      throw new AssertionError("Was not closed");
    }
  }
}
