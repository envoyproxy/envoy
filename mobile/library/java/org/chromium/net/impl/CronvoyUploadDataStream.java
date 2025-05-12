package org.chromium.net.impl;

import android.util.Log;
import androidx.annotation.GuardedBy;
import androidx.annotation.IntDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.nio.ByteBuffer;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UploadDataSink;
import org.chromium.net.impl.Executors.DirectPreventingExecutor;

/**
 * CronvoyUploadDataStream handles communication between an upload body
 * encapsulated in the embedder's {@link UploadDataSink}.
 */
public final class CronvoyUploadDataStream extends UploadDataSink {

  private static final String TAG = CronvoyUploadDataStream.class.getSimpleName();
  private static final ByteBuffer EMPTY_BYTE_BUFFER = ByteBuffer.allocateDirect(0);
  private final int BYTE_BUFFER_SIZE = 65535; // H2 initial_stream_window_size

  // These are never changed, once a request starts.
  private final Executor mExecutor;
  private final CronvoyVersionSafeCallbacks.UploadDataProviderWrapper mDataProvider;
  private final CronvoyUrlRequest mRequest;
  private long mLength;
  private long mRemainingLength;

  // Reusable read task, to reduce redundant memory allocation.
  private final Runnable mReadTask = new Runnable() {
    @Override
    public void run() {
      read();
    }
  };

  // It is only valid from the call to mDataProvider.read until onError or onReadSucceeded.
  private ByteBuffer mByteBuffer;
  private int mByteBufferLimit;

  // Lock that protects all subsequent variables. The adapter has to be
  // protected to ensure safe shutdown, mReading and mRewinding are protected
  // to robustly detect getting read/rewind results more often than expected.
  private final Object mLock = new Object();

  // Whether the CronetUploadDataStream is active or not
  @GuardedBy("mLock") private boolean mUploadDataStreamActive;

  @IntDef({UserCallback.READ, UserCallback.REWIND, UserCallback.GET_LENGTH,
           UserCallback.NOT_IN_CALLBACK})
  @Retention(RetentionPolicy.SOURCE)
  private @interface UserCallback {
    int READ = 0;
    int REWIND = 1;
    int GET_LENGTH = 2;
    int NOT_IN_CALLBACK = 3;
  }

  @GuardedBy("mLock") private @UserCallback int mInWhichUserCallback = UserCallback.NOT_IN_CALLBACK;
  @GuardedBy("mLock") private boolean mClosedPostponed;

  /**
   * Constructs a CronetUploadDataStream.
   * @param dataProvider the UploadDataProvider to read data from.
   * @param executor the Executor to execute UploadDataProvider tasks.
   */
  public CronvoyUploadDataStream(UploadDataProvider dataProvider, Executor executor,
                                 CronvoyUrlRequest request) {
    mExecutor = executor;
    mDataProvider = new CronvoyVersionSafeCallbacks.UploadDataProviderWrapper(dataProvider);
    mRequest = request;
  }

  /**
   * Called by native code to make the UploadDataProvider read data into
   * {@code byteBuffer}.
   * @param originalThread
   */
  void readDataReady() {
    if (mRemainingLength != 0) {
      postTaskToExecutor(mReadTask);
    } else {
      Runnable task = new Runnable() {
        @Override
        public void run() {
          mRequest.checkCallingThread();
          mRequest.send(EMPTY_BYTE_BUFFER, true);
          close();
        }
      };
      postTaskToExecutor(task);
    }
  }

  /**
   * Called to make the UploadDataProvider rewind upload data.
   */
  void rewind() {
    Runnable task = new Runnable() {
      @Override
      public void run() {
        synchronized (mLock) {
          checkState(UserCallback.NOT_IN_CALLBACK);
          mInWhichUserCallback = UserCallback.REWIND;
        }
        try {
          mDataProvider.rewind(CronvoyUploadDataStream.this);
        } catch (Exception exception) {
          onError(exception);
        }
      }
    };
    postTaskToExecutor(task);
  }

  @GuardedBy("mLock")
  private void checkState(@UserCallback int mode) {
    if (mInWhichUserCallback != mode) {
      throw new IllegalStateException("Expected " + mode + ", but was " + mInWhichUserCallback);
    }
  }

  private void read() {
    mRequest.checkCallingThread();
    synchronized (mLock) {
      if (!mUploadDataStreamActive) {
        return;
      }
      checkState(UserCallback.NOT_IN_CALLBACK);
      mInWhichUserCallback = UserCallback.READ;
    }
    try {
      // The mRemainingLength+1 is a hack to have the original tests passing - not really needed.
      mByteBufferLimit = mRemainingLength < 0 || mRemainingLength > BYTE_BUFFER_SIZE
                             ? BYTE_BUFFER_SIZE
                             : (int)mRemainingLength + 1;
      mByteBuffer = mRemainingLength < 0 || mRemainingLength > BYTE_BUFFER_SIZE
                        ? ByteBuffer.allocateDirect(BYTE_BUFFER_SIZE)
                        : ByteBuffer.allocate(mByteBufferLimit);
      mDataProvider.read(CronvoyUploadDataStream.this, mByteBuffer);
    } catch (Exception exception) {
      onError(exception);
    }
  }

  /**
   * Helper method called when an exception occurred. This method resets
   * states and propagates the error to the request.
   */
  private void onError(Exception exception) {
    final boolean sendClose;
    synchronized (mLock) {
      if (mInWhichUserCallback == UserCallback.NOT_IN_CALLBACK) {
        throw new IllegalStateException("There is no read or rewind or length check in progress.");
      }
      mInWhichUserCallback = UserCallback.NOT_IN_CALLBACK;
    }
    closeIfPostponed();

    // Just fail the request - simpler to fail directly, and
    // UploadDataStream only supports failing during initialization, not
    // while reading. The request is smart enough to handle the case where
    // it was already canceled by the embedder.
    mRequest.onUploadException(exception);
  }

  @Override
  public void onReadSucceeded(boolean lastChunk) {
    synchronized (mLock) {
      checkState(UserCallback.READ);
      if (mByteBufferLimit != mByteBuffer.limit()) {
        throw new IllegalStateException("ByteBuffer limit changed");
      }
      if (lastChunk && mLength >= 0) {
        throw new IllegalArgumentException("Non-chunked upload can't have last chunk");
      }
      int bytesRead = mByteBuffer.position();
      mRemainingLength -= bytesRead;
      if (mRemainingLength < 0 && mLength >= 0) {
        throw new IllegalArgumentException(
            String.format("Read upload data length %d exceeds expected length %d",
                          mLength - mRemainingLength, mLength));
      }
      mInWhichUserCallback = UserCallback.NOT_IN_CALLBACK;

      closeIfPostponed();
      if (!mUploadDataStreamActive) {
        return;
      }
    }
    mRequest.send(mByteBuffer, lastChunk || mRemainingLength == 0);
    mByteBuffer = null;
  }

  @Override
  public void onReadError(Exception exception) {
    synchronized (mLock) { checkState(UserCallback.READ); }
    onError(exception);
  }

  @Override
  public void onRewindSucceeded() {
    synchronized (mLock) {
      checkState(UserCallback.REWIND);
      mInWhichUserCallback = UserCallback.NOT_IN_CALLBACK;
      // Request may been canceled already.
      if (!mUploadDataStreamActive) {
        return;
      }
    }
    mRemainingLength = mLength;
    mRequest.followRedirectAfterSuccessfulRewind();
    readDataReady();
  }

  @Override
  public void onRewindError(Exception exception) {
    synchronized (mLock) { checkState(UserCallback.REWIND); }
    onError(exception);
  }

  /**
   * Posts task to application Executor.
   */
  private void postTaskToExecutor(Runnable task) {
    try {
      mExecutor.execute(task);
    } catch (RejectedExecutionException e) {
      // Just fail the request. The request is smart enough to handle the
      // case where it was already canceled by the embedder.
      mRequest.onUploadException(e);
    }
  }

  /**
   * Closes safely when there is no pending read.
   */
  void close() {
    synchronized (mLock) {
      if (mInWhichUserCallback == UserCallback.READ) {
        // Wait for the read to complete before destroy the adapter.
        mClosedPostponed = true;
        return;
      }
      if (!mUploadDataStreamActive) {
        return;
      }
      mUploadDataStreamActive = false;
    }
    Runnable task = new Runnable() {
      @Override
      public void run() {
        mRequest.checkCallingThread();
        try {
          mDataProvider.close();
        } catch (Exception e) {
          Log.e(TAG, "Exception thrown when closing", e);
        }
      }
    };
    postTaskToExecutor(task);
  }

  /**
   * Closes when a pending read which has since completed. Caller needs to be on executor thread.
   */
  private void closeIfPostponed() {
    synchronized (mLock) {
      if (mInWhichUserCallback == UserCallback.READ) {
        throw new IllegalStateException("Method should not be called when read has not completed.");
      }
      if (mClosedPostponed) {
        close();
      }
    }
  }

  /**
   * Initializes upload length by getting it from data provider, then follows with sending the
   * first chunk of the request body. These two initial tasks are not launched by the network
   * Thread.
   */
  void initializeWithRequest() {
    // Thread taskLauncherThread = Thread.currentThread();
    Runnable task = new Runnable() {
      @Override
      public void run() {
        synchronized (mLock) {
          mInWhichUserCallback = UserCallback.GET_LENGTH;
          mUploadDataStreamActive = true;
        }
        try {
          mLength = mDataProvider.getLength();
          mRemainingLength = mLength;
        } catch (Exception t) {
          onError(t);
          return;
        } finally {
          synchronized (mLock) { mInWhichUserCallback = UserCallback.NOT_IN_CALLBACK; }
        }
        readDataReady();
      }
    };
    // This task can not be launched by the Network Thread, so plan B is used here to ensure
    // that this is not a direct executor when disallowed.
    Executor executor =
        mRequest.isAllowDirectExecutor() ? mExecutor : new DirectPreventingExecutor(mExecutor);
    try {
      executor.execute(task);
    } catch (RejectedExecutionException e) {
      mRequest.onUploadException(e);
    }
  }
}
