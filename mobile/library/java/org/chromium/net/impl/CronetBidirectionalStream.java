package org.chromium.net.impl;

import android.util.Log;
import androidx.annotation.GuardedBy;
import androidx.annotation.IntDef;
import androidx.annotation.VisibleForTesting;
import io.envoyproxy.envoymobile.Engine;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.nio.ByteBuffer;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import org.chromium.net.BidirectionalStream;
import org.chromium.net.CallbackException;
import org.chromium.net.CronetException;
import org.chromium.net.ExperimentalBidirectionalStream;
import org.chromium.net.NetworkException;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UrlResponseInfo;
import org.chromium.net.impl.Annotations.RequestPriority;

/**
 * {@link BidirectionalStream} implementation using Envoy-Mobile stack.
 * All @CalledByNative methods are called on the native network thread
 * and post tasks with callback calls onto Executor. Upon returning from callback, the native
 * stream is called on Executor thread and posts native tasks to the native network thread.
 */
final class CronetBidirectionalStream extends ExperimentalBidirectionalStream {
  /**
   * States of BidirectionalStream are tracked in mReadState and mWriteState.
   * The write state is separated out as it changes independently of the read state.
   * There is one initial state: State.NOT_STARTED. There is one normal final state:
   * State.SUCCESS, reached after State.READING_DONE and State.WRITING_DONE. There are two
   * exceptional final states: State.CANCELED and State.ERROR, which can be reached from
   * any other non-final state.
   */
  @IntDef({State.NOT_STARTED, State.STARTED, State.WAITING_FOR_READ, State.READING,
           State.READING_DONE, State.CANCELED, State.ERROR, State.SUCCESS, State.WAITING_FOR_FLUSH,
           State.WRITING, State.WRITING_DONE})
  @Retention(RetentionPolicy.SOURCE)
  private @interface State {
    /* Initial state, stream not started. */
    int NOT_STARTED = 0;
    /*
     * Stream started, request headers are being sent if mDelayRequestHeadersUntilNextFlush
     * is not set to true.
     */
    int STARTED = 1;
    /* Waiting for {@code read()} to be called. */
    int WAITING_FOR_READ = 2;
    /* Reading from the remote, {@code onReadCompleted()} callback will be called when done. */
    int READING = 3;
    /* There is no more data to read and stream is half-closed by the remote side. */
    int READING_DONE = 4;
    /* Stream is canceled. */
    int CANCELED = 5;
    /* Error has occurred, stream is closed. */
    int ERROR = 6;
    /* Reading and writing are done, and the stream is closed successfully. */
    int SUCCESS = 7;
    /* Waiting for {@code CronetBidirectionalStreamJni.get().sendRequestHeaders()} or {@code
       CronetBidirectionalStreamJni.get().writevData()} to be called. */
    int WAITING_FOR_FLUSH = 8;
    /* Writing to the remote, {@code onWritevCompleted()} callback will be called when done. */
    int WRITING = 9;
    /* There is no more data to write and stream is half-closed by the local side. */
    int WRITING_DONE = 10;
  }

  private final CronetUrlRequestContext mRequestContext;
  private final Executor mExecutor;
  private final VersionSafeCallbacks.BidirectionalStreamCallback mCallback;
  private final String mInitialUrl;
  private final int mInitialPriority;
  private final String mInitialMethod;
  private final String[] mRequestHeaders;
  private final boolean mDelayRequestHeadersUntilFirstFlush;
  private final Collection<Object> mRequestAnnotations;
  private final boolean mTrafficStatsTagSet;
  private final int mTrafficStatsTag;
  private final boolean mTrafficStatsUidSet;
  private final int mTrafficStatsUid;
  private CronetException mException;

  /*
   * Synchronizes access to mNativeStream, mReadState and mWriteState.
   */
  private final Object mNativeStreamLock = new Object();

  @GuardedBy("mNativeStreamLock")
  // Pending write data.
  private LinkedList<ByteBuffer> mPendingData;

  @GuardedBy("mNativeStreamLock")
  // Flush data queue that should be pushed to the native stack when the previous
  // CronetBidirectionalStreamJni.get().writevData completes.
  private LinkedList<ByteBuffer> mFlushData;

  @GuardedBy("mNativeStreamLock")
  // Whether an end-of-stream flag is passed in through write().
  private boolean mEndOfStreamWritten;

  @GuardedBy("mNativeStreamLock")
  // Whether request headers have been sent.
  private boolean mRequestHeadersSent;

  @GuardedBy("mNativeStreamLock")
  // Metrics information. Obtained when request succeeds, fails or is canceled.
  private RequestFinishedInfo.Metrics mMetrics;

  /* Native BidirectionalStream object, owned by CronetBidirectionalStream. */
  @GuardedBy("mNativeStreamLock") private long mNativeStream;

  /**
   * Read state is tracking reading flow.
   *                         / <--- READING <--- \
   *                         |                   |
   *                         \                   /
   * NOT_STARTED -> STARTED --> WAITING_FOR_READ -> READING_DONE -> SUCCESS
   */
  @GuardedBy("mNativeStreamLock") private @State int mReadState = State.NOT_STARTED;

  /**
   * Write state is tracking writing flow.
   *                         / <---  WRITING  <--- \
   *                         |                     |
   *                         \                     /
   * NOT_STARTED -> STARTED --> WAITING_FOR_FLUSH -> WRITING_DONE -> SUCCESS
   */
  @GuardedBy("mNativeStreamLock") private @State int mWriteState = State.NOT_STARTED;

  // Only modified on the network thread.
  private UrlResponseInfoImpl mResponseInfo;

  /*
   * OnReadCompleted callback is repeatedly invoked when each read is completed, so it
   * is cached as a member variable.
   */
  // Only modified on the network thread.
  private OnReadCompletedRunnable mOnReadCompletedTask;

  private Runnable mOnDestroyedCallbackForTesting;

  private final class OnReadCompletedRunnable implements Runnable {
    // Buffer passed back from current invocation of onReadCompleted.
    ByteBuffer mByteBuffer;
    // End of stream flag from current invocation of onReadCompleted.
    boolean mEndOfStream;

    @Override
    public void run() {
      try {
        // Null out mByteBuffer, to pass buffer ownership to callback or release if done.
        ByteBuffer buffer = mByteBuffer;
        mByteBuffer = null;
        boolean maybeOnSucceeded = false;
        synchronized (mNativeStreamLock) {
          if (isDoneLocked()) {
            return;
          }
          if (mEndOfStream) {
            mReadState = State.READING_DONE;
            maybeOnSucceeded = (mWriteState == State.WRITING_DONE);
          } else {
            mReadState = State.WAITING_FOR_READ;
          }
        }
        mCallback.onReadCompleted(CronetBidirectionalStream.this, mResponseInfo, buffer,
                                  mEndOfStream);
        if (maybeOnSucceeded) {
          maybeOnSucceededOnExecutor();
        }
      } catch (Exception e) {
        onCallbackException(e);
      }
    }
  }

  private final class OnWriteCompletedRunnable implements Runnable {
    // Buffer passed back from current invocation of onWriteCompleted.
    private ByteBuffer mByteBuffer;
    // End of stream flag from current call to write.
    private final boolean mEndOfStream;

    OnWriteCompletedRunnable(ByteBuffer buffer, boolean endOfStream) {
      mByteBuffer = buffer;
      mEndOfStream = endOfStream;
    }

    @Override
    public void run() {
      try {
        // Null out mByteBuffer, to pass buffer ownership to callback or release if done.
        ByteBuffer buffer = mByteBuffer;
        mByteBuffer = null;
        boolean maybeOnSucceeded = false;
        synchronized (mNativeStreamLock) {
          if (isDoneLocked()) {
            return;
          }
          if (mEndOfStream) {
            mWriteState = State.WRITING_DONE;
            maybeOnSucceeded = (mReadState == State.READING_DONE);
          }
        }
        mCallback.onWriteCompleted(CronetBidirectionalStream.this, mResponseInfo, buffer,
                                   mEndOfStream);
        if (maybeOnSucceeded) {
          maybeOnSucceededOnExecutor();
        }
      } catch (Exception e) {
        onCallbackException(e);
      }
    }
  }

  CronetBidirectionalStream(CronetUrlRequestContext requestContext, String url,
                            @CronetEngineBase.StreamPriority int priority, Callback callback,
                            Executor executor, String httpMethod,
                            List<Map.Entry<String, String>> requestHeaders,
                            boolean delayRequestHeadersUntilNextFlush,
                            Collection<Object> requestAnnotations, boolean trafficStatsTagSet,
                            int trafficStatsTag, boolean trafficStatsUidSet, int trafficStatsUid) {
    mRequestContext = requestContext;
    mInitialUrl = url;
    mInitialPriority = convertStreamPriority(priority);
    mCallback = new VersionSafeCallbacks.BidirectionalStreamCallback(callback);
    mExecutor = executor;
    mInitialMethod = httpMethod;
    mRequestHeaders = stringsFromHeaderList(requestHeaders);
    mDelayRequestHeadersUntilFirstFlush = delayRequestHeadersUntilNextFlush;
    mPendingData = new LinkedList<>();
    mFlushData = new LinkedList<>();
    mRequestAnnotations = requestAnnotations;
    mTrafficStatsTagSet = trafficStatsTagSet;
    mTrafficStatsTag = trafficStatsTag;
    mTrafficStatsUidSet = trafficStatsUidSet;
    mTrafficStatsUid = trafficStatsUid;
  }

  @Override
  public void start() {
    synchronized (mNativeStreamLock) {
      if (mReadState != State.NOT_STARTED) {
        throw new IllegalStateException("Stream is already started.");
      }
      try {
        mNativeStream = CronetBidirectionalStreamJni.get().createBidirectionalStream(
            CronetBidirectionalStream.this, mRequestContext.getEnvoyEngine(),
            !mDelayRequestHeadersUntilFirstFlush, mRequestContext.hasRequestFinishedListener(),
            mTrafficStatsTagSet, mTrafficStatsTag, mTrafficStatsUidSet, mTrafficStatsUid);
        mRequestContext.onRequestStarted();
        // Non-zero startResult means an argument error.
        int startResult = CronetBidirectionalStreamJni.get().start(
            mNativeStream, CronetBidirectionalStream.this, mInitialUrl, mInitialPriority,
            mInitialMethod, mRequestHeaders, !doesMethodAllowWriteData(mInitialMethod));
        if (startResult == -1) {
          throw new IllegalArgumentException("Invalid http method " + mInitialMethod);
        }
        if (startResult > 0) {
          int headerPos = startResult - 1;
          throw new IllegalArgumentException("Invalid header " + mRequestHeaders[headerPos] + "=" +
                                             mRequestHeaders[headerPos + 1]);
        }
        mReadState = mWriteState = State.STARTED;
      } catch (RuntimeException e) {
        // If there's an exception, clean up and then throw the
        // exception to the caller.
        destroyNativeStreamLocked(false);
        throw e;
      }
    }
  }

  @Override
  public void read(ByteBuffer buffer) {
    synchronized (mNativeStreamLock) {
      Preconditions.checkHasRemaining(buffer);
      Preconditions.checkDirect(buffer);
      if (mReadState != State.WAITING_FOR_READ) {
        throw new IllegalStateException("Unexpected read attempt.");
      }
      if (isDoneLocked()) {
        return;
      }
      if (mOnReadCompletedTask == null) {
        mOnReadCompletedTask = new OnReadCompletedRunnable();
      }
      mReadState = State.READING;
      if (!CronetBidirectionalStreamJni.get().readData(mNativeStream,
                                                       CronetBidirectionalStream.this, buffer,
                                                       buffer.position(), buffer.limit())) {
        // Still waiting on read. This is just to have consistent
        // behavior with the other error cases.
        mReadState = State.WAITING_FOR_READ;
        throw new IllegalArgumentException("Unable to call native read");
      }
    }
  }

  @Override
  public void write(ByteBuffer buffer, boolean endOfStream) {
    synchronized (mNativeStreamLock) {
      Preconditions.checkDirect(buffer);
      if (!buffer.hasRemaining() && !endOfStream) {
        throw new IllegalArgumentException("Empty buffer before end of stream.");
      }
      if (mEndOfStreamWritten) {
        throw new IllegalArgumentException("Write after writing end of stream.");
      }
      if (isDoneLocked()) {
        return;
      }
      mPendingData.add(buffer);
      if (endOfStream) {
        mEndOfStreamWritten = true;
      }
    }
  }

  @Override
  public void flush() {
    synchronized (mNativeStreamLock) {
      if (isDoneLocked() ||
          (mWriteState != State.WAITING_FOR_FLUSH && mWriteState != State.WRITING)) {
        return;
      }
      if (mPendingData.isEmpty() && mFlushData.isEmpty()) {
        // If there is no pending write when flush() is called, see if
        // request headers need to be flushed.
        if (!mRequestHeadersSent) {
          mRequestHeadersSent = true;
          CronetBidirectionalStreamJni.get().sendRequestHeaders(mNativeStream,
                                                                CronetBidirectionalStream.this);
          if (!doesMethodAllowWriteData(mInitialMethod)) {
            mWriteState = State.WRITING_DONE;
          }
        }
        return;
      }

      assert !mPendingData.isEmpty() || !mFlushData.isEmpty();

      // Move buffers from mPendingData to the flushing queue.
      if (!mPendingData.isEmpty()) {
        mFlushData.addAll(mPendingData);
        mPendingData.clear();
      }

      if (mWriteState == State.WRITING) {
        // If there is a write already pending, wait until onWritevCompleted is
        // called before pushing data to the native stack.
        return;
      }
      sendFlushDataLocked();
    }
  }

  // Helper method to send buffers in mFlushData. Caller needs to acquire
  // mNativeStreamLock and make sure mWriteState is WAITING_FOR_FLUSH and
  // mFlushData queue isn't empty.
  @SuppressWarnings("GuardedByChecker")
  private void sendFlushDataLocked() {
    assert mWriteState == State.WAITING_FOR_FLUSH;
    int size = mFlushData.size();
    ByteBuffer[] buffers = new ByteBuffer[size];
    int[] positions = new int[size];
    int[] limits = new int[size];
    for (int i = 0; i < size; i++) {
      ByteBuffer buffer = mFlushData.poll();
      buffers[i] = buffer;
      positions[i] = buffer.position();
      limits[i] = buffer.limit();
    }
    assert mFlushData.isEmpty();
    assert buffers.length >= 1;
    mWriteState = State.WRITING;
    mRequestHeadersSent = true;
    if (!CronetBidirectionalStreamJni.get().writevData(
            mNativeStream, CronetBidirectionalStream.this, buffers, positions, limits,
            mEndOfStreamWritten && mPendingData.isEmpty())) {
      // Still waiting on flush. This is just to have consistent
      // behavior with the other error cases.
      mWriteState = State.WAITING_FOR_FLUSH;
      throw new IllegalArgumentException("Unable to call native writev.");
    }
  }

  /**
   * Returns a read-only copy of {@code mPendingData} for testing.
   */
  @VisibleForTesting
  public List<ByteBuffer> getPendingDataForTesting() {
    synchronized (mNativeStreamLock) {
      List<ByteBuffer> pendingData = new LinkedList<ByteBuffer>();
      for (ByteBuffer buffer : mPendingData) {
        pendingData.add(buffer.asReadOnlyBuffer());
      }
      return pendingData;
    }
  }

  /**
   * Returns a read-only copy of {@code mFlushData} for testing.
   */
  @VisibleForTesting
  public List<ByteBuffer> getFlushDataForTesting() {
    synchronized (mNativeStreamLock) {
      List<ByteBuffer> flushData = new LinkedList<ByteBuffer>();
      for (ByteBuffer buffer : mFlushData) {
        flushData.add(buffer.asReadOnlyBuffer());
      }
      return flushData;
    }
  }

  @Override
  public void cancel() {
    synchronized (mNativeStreamLock) {
      if (isDoneLocked() || mReadState == State.NOT_STARTED) {
        return;
      }
      mReadState = mWriteState = State.CANCELED;
      destroyNativeStreamLocked(true);
    }
  }

  @Override
  public boolean isDone() {
    synchronized (mNativeStreamLock) { return isDoneLocked(); }
  }

  @GuardedBy("mNativeStreamLock")
  private boolean isDoneLocked() {
    return mReadState != State.NOT_STARTED && mNativeStream == 0;
  }

  /*
   * Runs an onSucceeded callback if both Read and Write sides are closed.
   */
  private void maybeOnSucceededOnExecutor() {
    synchronized (mNativeStreamLock) {
      if (isDoneLocked()) {
        return;
      }
      if (!(mWriteState == State.WRITING_DONE && mReadState == State.READING_DONE)) {
        return;
      }
      mReadState = mWriteState = State.SUCCESS;
      // Destroy native stream first, so UrlRequestContext could be shut
      // down from the listener.
      destroyNativeStreamLocked(false);
    }
    try {
      mCallback.onSucceeded(CronetBidirectionalStream.this, mResponseInfo);
    } catch (Exception e) {
      Log.e(CronetUrlRequestContext.LOG_TAG, "Exception in onSucceeded method", e);
    }
  }

  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onStreamReady(final boolean requestHeadersSent) {
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        synchronized (mNativeStreamLock) {
          if (isDoneLocked()) {
            return;
          }
          mRequestHeadersSent = requestHeadersSent;
          mReadState = State.WAITING_FOR_READ;
          if (!doesMethodAllowWriteData(mInitialMethod) && mRequestHeadersSent) {
            mWriteState = State.WRITING_DONE;
          } else {
            mWriteState = State.WAITING_FOR_FLUSH;
          }
        }

        try {
          mCallback.onStreamReady(CronetBidirectionalStream.this);
        } catch (Exception e) {
          onCallbackException(e);
        }
      }
    });
  }

  /**
   * Called when the final set of headers, after all redirects,
   * is received. Can only be called once for each stream.
   */
  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onResponseHeadersReceived(int httpStatusCode, String negotiatedProtocol,
                                         String[] headers, long receivedByteCount) {
    try {
      mResponseInfo = prepareResponseInfoOnNetworkThread(httpStatusCode, negotiatedProtocol,
                                                         headers, receivedByteCount);
    } catch (Exception e) {
      failWithException(new CronetExceptionImpl("Cannot prepare ResponseInfo", null));
      return;
    }
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        synchronized (mNativeStreamLock) {
          if (isDoneLocked()) {
            return;
          }
          mReadState = State.WAITING_FOR_READ;
        }

        try {
          mCallback.onResponseHeadersReceived(CronetBidirectionalStream.this, mResponseInfo);
        } catch (Exception e) {
          onCallbackException(e);
        }
      }
    });
  }

  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onReadCompleted(final ByteBuffer byteBuffer, int bytesRead, int initialPosition,
                               int initialLimit, long receivedByteCount) {
    mResponseInfo.setReceivedByteCount(receivedByteCount);
    if (byteBuffer.position() != initialPosition || byteBuffer.limit() != initialLimit) {
      failWithException(
          new CronetExceptionImpl("ByteBuffer modified externally during read", null));
      return;
    }
    if (bytesRead < 0 || initialPosition + bytesRead > initialLimit) {
      failWithException(new CronetExceptionImpl("Invalid number of bytes read", null));
      return;
    }
    byteBuffer.position(initialPosition + bytesRead);
    assert mOnReadCompletedTask.mByteBuffer == null;
    mOnReadCompletedTask.mByteBuffer = byteBuffer;
    mOnReadCompletedTask.mEndOfStream = (bytesRead == 0);
    postTaskToExecutor(mOnReadCompletedTask);
  }

  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onWritevCompleted(final ByteBuffer[] byteBuffers, int[] initialPositions,
                                 int[] initialLimits, boolean endOfStream) {
    assert byteBuffers.length == initialPositions.length;
    assert byteBuffers.length == initialLimits.length;
    synchronized (mNativeStreamLock) {
      if (isDoneLocked())
        return;
      mWriteState = State.WAITING_FOR_FLUSH;
      // Flush if there is anything in the flush queue mFlushData.
      if (!mFlushData.isEmpty()) {
        sendFlushDataLocked();
      }
    }
    for (int i = 0; i < byteBuffers.length; i++) {
      ByteBuffer buffer = byteBuffers[i];
      if (buffer.position() != initialPositions[i] || buffer.limit() != initialLimits[i]) {
        failWithException(
            new CronetExceptionImpl("ByteBuffer modified externally during write", null));
        return;
      }
      // Current implementation always writes the complete buffer.
      buffer.position(buffer.limit());
      postTaskToExecutor(new OnWriteCompletedRunnable(
          buffer,
          // Only set endOfStream flag if this buffer is the last in byteBuffers.
          endOfStream && i == byteBuffers.length - 1));
    }
  }

  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onResponseTrailersReceived(String[] trailers) {
    final UrlResponseInfo.HeaderBlock trailersBlock =
        new UrlResponseInfoImpl.HeaderBlockImpl(headersListFromStrings(trailers));
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        synchronized (mNativeStreamLock) {
          if (isDoneLocked()) {
            return;
          }
        }
        try {
          mCallback.onResponseTrailersReceived(CronetBidirectionalStream.this, mResponseInfo,
                                               trailersBlock);
        } catch (Exception e) {
          onCallbackException(e);
        }
      }
    });
  }

  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onError(int errorCode, int nativeError, int nativeQuicError, String errorString,
                       long receivedByteCount) {
    if (mResponseInfo != null) {
      mResponseInfo.setReceivedByteCount(receivedByteCount);
    }
    if (errorCode == NetworkException.ERROR_QUIC_PROTOCOL_FAILED ||
        errorCode == NetworkException.ERROR_NETWORK_CHANGED) {
      failWithException(new QuicExceptionImpl("Exception in BidirectionalStream: " + errorString,
                                              errorCode, nativeError, nativeQuicError));
    } else {
      failWithException(new BidirectionalStreamNetworkException(
          "Exception in BidirectionalStream: " + errorString, errorCode, nativeError));
    }
  }

  /**
   * Called when request is canceled, no callbacks will be called afterwards.
   */
  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onCanceled() {
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        try {
          mCallback.onCanceled(CronetBidirectionalStream.this, mResponseInfo);
        } catch (Exception e) {
          Log.e(CronetUrlRequestContext.LOG_TAG, "Exception in onCanceled method", e);
        }
      }
    });
  }

  /**
   * Called by the native code to report metrics just before the native adapter is destroyed.
   */
  @SuppressWarnings("unused")
  // TODO(carloseltuerto) Hook up Envoy-Mobile to call back this method.
  private void onMetricsCollected(long requestStartMs, long dnsStartMs, long dnsEndMs,
                                  long connectStartMs, long connectEndMs, long sslStartMs,
                                  long sslEndMs, long sendingStartMs, long sendingEndMs,
                                  long pushStartMs, long pushEndMs, long responseStartMs,
                                  long requestEndMs, boolean socketReused, long sentByteCount,
                                  long receivedByteCount) {
    synchronized (mNativeStreamLock) {
      if (mMetrics != null) {
        throw new IllegalStateException("Metrics collection should only happen once.");
      }
      mMetrics = new CronetMetrics(requestStartMs, dnsStartMs, dnsEndMs, connectStartMs,
                                   connectEndMs, sslStartMs, sslEndMs, sendingStartMs, sendingEndMs,
                                   pushStartMs, pushEndMs, responseStartMs, requestEndMs,
                                   socketReused, sentByteCount, receivedByteCount);
      assert mReadState == mWriteState;
      assert (mReadState == State.SUCCESS) || (mReadState == State.ERROR) ||
          (mReadState == State.CANCELED);
      int finishedReason;
      if (mReadState == State.SUCCESS) {
        finishedReason = RequestFinishedInfo.SUCCEEDED;
      } else if (mReadState == State.CANCELED) {
        finishedReason = RequestFinishedInfo.CANCELED;
      } else {
        finishedReason = RequestFinishedInfo.FAILED;
      }
      final RequestFinishedInfo requestFinishedInfo = new RequestFinishedInfoImpl(
          mInitialUrl, mRequestAnnotations, mMetrics, finishedReason, mResponseInfo, mException);
      mRequestContext.reportRequestFinished(requestFinishedInfo);
    }
  }

  @VisibleForTesting
  public void setOnDestroyedCallbackForTesting(Runnable onDestroyedCallbackForTesting) {
    mOnDestroyedCallbackForTesting = onDestroyedCallbackForTesting;
  }

  private static boolean doesMethodAllowWriteData(String methodName) {
    return !methodName.equals("GET") && !methodName.equals("HEAD");
  }

  private static ArrayList<Map.Entry<String, String>> headersListFromStrings(String[] headers) {
    ArrayList<Map.Entry<String, String>> headersList = new ArrayList<>(headers.length / 2);
    for (int i = 0; i < headers.length; i += 2) {
      headersList.add(new AbstractMap.SimpleImmutableEntry<>(headers[i], headers[i + 1]));
    }
    return headersList;
  }

  private static String[] stringsFromHeaderList(List<Map.Entry<String, String>> headersList) {
    String headersArray[] = new String[headersList.size() * 2];
    int i = 0;
    for (Map.Entry<String, String> requestHeader : headersList) {
      headersArray[i++] = requestHeader.getKey();
      headersArray[i++] = requestHeader.getValue();
    }
    return headersArray;
  }

  private static int convertStreamPriority(@CronetEngineBase.StreamPriority int priority) {
    switch (priority) {
    case Builder.STREAM_PRIORITY_IDLE:
      return RequestPriority.IDLE;
    case Builder.STREAM_PRIORITY_LOWEST:
      return RequestPriority.LOWEST;
    case Builder.STREAM_PRIORITY_LOW:
      return RequestPriority.LOW;
    case Builder.STREAM_PRIORITY_MEDIUM:
      return RequestPriority.MEDIUM;
    case Builder.STREAM_PRIORITY_HIGHEST:
      return RequestPriority.HIGHEST;
    default:
      throw new IllegalArgumentException("Invalid stream priority.");
    }
  }

  /**
   * Posts task to application Executor. Used for callbacks
   * and other tasks that should not be executed on network thread.
   */
  private void postTaskToExecutor(Runnable task) {
    try {
      mExecutor.execute(task);
    } catch (RejectedExecutionException failException) {
      Log.e(CronetUrlRequestContext.LOG_TAG, "Exception posting task to executor", failException);
      // If posting a task throws an exception, then there is no choice
      // but to destroy the stream without invoking the callback.
      synchronized (mNativeStreamLock) {
        mReadState = mWriteState = State.ERROR;
        destroyNativeStreamLocked(false);
      }
    }
  }

  private UrlResponseInfoImpl prepareResponseInfoOnNetworkThread(int httpStatusCode,
                                                                 String negotiatedProtocol,
                                                                 String[] headers,
                                                                 long receivedByteCount) {
    UrlResponseInfoImpl responseInfo = new UrlResponseInfoImpl(
        Arrays.asList(mInitialUrl), httpStatusCode, "", headersListFromStrings(headers), false,
        negotiatedProtocol, null, receivedByteCount);
    return responseInfo;
  }

  @GuardedBy("mNativeStreamLock")
  private void destroyNativeStreamLocked(boolean sendOnCanceled) {
    Log.i(CronetUrlRequestContext.LOG_TAG, "destroyNativeStreamLocked " + this.toString());
    if (mNativeStream == 0) {
      return;
    }
    CronetBidirectionalStreamJni.get().destroy(mNativeStream, CronetBidirectionalStream.this,
                                               sendOnCanceled);
    mRequestContext.onRequestDestroyed();
    mNativeStream = 0;
    if (mOnDestroyedCallbackForTesting != null) {
      mOnDestroyedCallbackForTesting.run();
    }
  }

  /**
   * Fails the stream with an exception. Only called on the Executor.
   */
  private void failWithExceptionOnExecutor(CronetException e) {
    mException = e;
    // Do not call into mCallback if request is complete.
    synchronized (mNativeStreamLock) {
      if (isDoneLocked()) {
        return;
      }
      mReadState = mWriteState = State.ERROR;
      destroyNativeStreamLocked(false);
    }
    try {
      mCallback.onFailed(this, mResponseInfo, e);
    } catch (Exception failException) {
      Log.e(CronetUrlRequestContext.LOG_TAG, "Exception notifying of failed request",
            failException);
    }
  }

  /**
   * If callback method throws an exception, stream gets canceled
   * and exception is reported via onFailed callback.
   * Only called on the Executor.
   */
  private void onCallbackException(Exception e) {
    CallbackException streamError =
        new CallbackExceptionImpl("CalledByNative method has thrown an exception", e);
    Log.e(CronetUrlRequestContext.LOG_TAG, "Exception in CalledByNative method", e);
    failWithExceptionOnExecutor(streamError);
  }

  /**
   * Fails the stream with an exception. Can be called on any thread.
   */
  private void failWithException(final CronetException exception) {
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        failWithExceptionOnExecutor(exception);
      }
    });
  }

  interface CronetBidirectionalStreamJni {
    long createBidirectionalStream(CronetBidirectionalStream caller, Engine envoyEngine,
                                   boolean sendRequestHeadersAutomatically,
                                   boolean enableMetricsCollection, boolean trafficStatsTagSet,
                                   int trafficStatsTag, boolean trafficStatsUidSet,
                                   int trafficStatsUid);

    int start(long nativePtr, CronetBidirectionalStream caller, String url, int priority,
              String method, String[] headers, boolean endOfStream);

    void sendRequestHeaders(long nativePtr, CronetBidirectionalStream caller);

    boolean readData(long nativePtr, CronetBidirectionalStream caller, ByteBuffer byteBuffer,
                     int position, int limit);

    boolean writevData(long nativePtr, CronetBidirectionalStream caller, ByteBuffer[] buffers,
                       int[] positions, int[] limits, boolean endOfStream);

    void destroy(long nativePtr, CronetBidirectionalStream caller, boolean sendOnCanceled);

    static CronetBidirectionalStreamJni get() {
      return null; // TODO(carloseltuerto) Implement!
    }
  }
}
