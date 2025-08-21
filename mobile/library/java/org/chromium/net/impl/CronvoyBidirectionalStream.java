package org.chromium.net.impl;

import static org.chromium.net.impl.Errors.isQuicException;
import static org.chromium.net.impl.Errors.mapEnvoyMobileErrorToNetError;
import static org.chromium.net.impl.Errors.mapNetErrorToCronetApiErrorCode;

import android.util.Log;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.net.BidirectionalStream;
import org.chromium.net.CallbackException;
import org.chromium.net.CronetException;
import org.chromium.net.ExperimentalBidirectionalStream;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UrlResponseInfo;
import org.chromium.net.impl.Annotations.RequestPriority;
import org.chromium.net.impl.CronvoyBidirectionalState.Event;
import org.chromium.net.impl.CronvoyBidirectionalState.NextAction;
import org.chromium.net.impl.Errors.NetError;
import org.chromium.net.impl.CronvoyUrlResponseInfoImpl.HeaderBlockImpl;

import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedDeque;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel;

/**
 * {@link BidirectionalStream} implementation using Envoy-Mobile stack.
 *
 * <p><b>C++ API differences between EM and Cronet</b>:
 * <br>The Cronet C++ API was carved to make the Java implementation of BidirectionalStream
 * straightforward. EM C++ API is more bare bone. The missing/different logic is therefore being
 * handled by this class. Here are the main differences:
 * <ul>
 * <li>onStreamReady is called by the C++ on the Network Thread. EM does not have that.
 * <li>For the request body, Cronet C++ does a callback once a ByteBuffer has been taking in charge.
 * EM rather does a callback once it is ready to take in charge the next ByteBuffer.
 * <li>The Cronet C++ "write" method accepts a list of ByteBuffers. EM C++ "write" method accepts
 * a single ByteBuffer. This feature is important for QUIC - see Issue #2264
 * <li>Cronet C++ systematically does a final "onReadCompleted" callback with an empty ByteBuffer.
 * This is the way to tell the user that there is nothing more coming in. EM C++ does not do that:
 * the last ByteBuffer might not be empty.
 * <li>Cronet C++ does a single "onReadCompleted" callback with an empty ByteBuffer when there is no
 * Response Body. EM C++ does nothing like that.
 * <li>Cronet has a specific C++ API to destroy the stream (not just "cancel"). This allows Cronet
 * to report an Error and quit immediately by invoking "destroy". EM only has Cancel. An EM stream
 * is deemed destroyed only once one of the 3 terminating EM callbacks has been invoked.
 * <li>When invoking "cancel" with Cronet, it is guaranteed that the Cronet C++ with invoke the
 * "onCancel" callback. EM does not do the same: if the "endOfStream" for both "read" and "write"
 * have been recorded by the EM C++, then invoking "cancel" just before receiving an EM terminal
 * callback will not have "onCancel" to be invoked. For example, if a "cancel" is requested when
 * executing "onData" callback method with "endOfStream == true", then this situation occurs:
 * "onComplete" will be called, not "onCancel".
 * </ul>
 *
 * <p><b>Implementation strategy</b>:
 * <br>Implementation wise, the most noticeable difference between the Cronet implementation and
 * this one is the avoidance of any java "synchronized". This implementation is based on "Compare
 * And Swap" logic to guarantee correctness. The State of the Stream is kept in a single atomic
 * Integer owned by {@link CronvoyBidirectionalState}. That state is a set of bits. Technically it
 * could have been the conjunction of Enums held inside a single Integer. Using bits turned out
 * to avoid more complex "if" logic. Still, the most important point here is the fact that the whole
 * state is a single Atomic Integer: it eases the avoidance of race conditions, especially when
 * "cancel" is involved.
 * <ul>
 * <li>When starting, the EM Engine itself might still not have finished its own initialisation.
 * This implementation won't block. Instead, the Stream creation will be piggybacked on the Engine
 * initialisation completion callback. The {@link #start} method is therefore non-blocking like all
 * all other Stream methods.
 * <li>The User "onStreamReady" callback is invoked by this class after creating the Stream, or
 * after sending the Request Headers when requested to do so immediately. EM does not have that C++
 * callback.
 * <li>Since EM does not indicate when the last "sendData" was taken in charge by EM, then invoking
 * "sendData" with "endOfStream == true" also takes care of scheduling the last User
 * "onWriteCompleted" callback. This might look like incorrect, but in reality this does not affect
 * at all the overall behaviour.
 * <li>When invoking "sendData", if the position of the ByteBuffer is not zero, then the ByteBuffer
 * is copied so the data starts at position zero. See Issue #2247.
 * <li>EM does not expose a callback method where the last received ByteBuffer is empty. This Java
 * implementation fakes that behaviour. When the logic figures out that EM has received its last
 * ByteBuffer (usually not empty), then the State logic is set to wait for an ultimate "read" from
 * the User. This can occur after the Stream has competed. Upon receiving the last User "read", a
 * User "onReadCompleted" callback is immediately scheduled with an empty ByteBuffer.
 * <li>The "cancel" request is asynchronous. It can happen in the 6 steps of the life cycle of a
 * Stream: before starting, while starting, after starting, after receiving final "endOfStream",
 * after receiving a terminating EM callback, and after finishing the request. For the first and
 * last state, it is easy: nothing to do. When starting, any "cancel" must be postponed until
 * started. A "cancel" after receiving final "endOfStream" may or may not be processed by the EM
 * "onCancel" callback. In this case any terminal EM callback will complete the "cancel". If a
 * "cancel" occurs after receiving a terminal EM callback then the User "onCanceled" callback is
 * invoked immediately. And to avoid invoking further Stream methods once "cancel" has been invoked,
 * a dedicated class handles this business: {@link CancelProofEnvoyStream}.
 * </ul>
 */
public final class CronvoyBidirectionalStream
    extends ExperimentalBidirectionalStream implements EnvoyHTTPCallbacks {

  private static final String X_ENVOY = "x-envoy";
  private static final String X_ENVOY_SELECTED_TRANSPORT = "x-envoy-upstream-alpn";
  private static final String USER_AGENT = "User-Agent";

  private final CronvoyUrlRequestContext mRequestContext;
  private final Executor mExecutor;
  private final CronvoyVersionSafeCallbacks.BidirectionalStreamCallback mCallback;
  private final String mInitialUrl;
  // TODO(https://github.com/envoyproxy/envoy-mobile/issues/1641): Priority? What should we do.
  private final int mInitialPriority;
  private final String mMethod;
  private final boolean mReadOnly; // if mInitialMethod is GET or HEAD, then this is true.
  private final List<Map.Entry<String, String>> mRequestHeaders;
  private final boolean mDelayRequestHeadersUntilFirstFlush;
  private final Collection<Object> mRequestAnnotations;
  // TODO(https://github.com/envoyproxy/envoy-mobile/issues/1521): implement traffic tagging.
  private final boolean mTrafficStatsTagSet;
  private final int mTrafficStatsTag;
  private final boolean mTrafficStatsUidSet;
  private final int mTrafficStatsUid;
  private final String mUserAgent;
  private final CancelProofEnvoyStream mStream = new CancelProofEnvoyStream();
  private final CronvoyBidirectionalState mState = new CronvoyBidirectionalState();
  private final AtomicInteger mUserflushConcurrentInvocationCount = new AtomicInteger();
  private final AtomicInteger mFlushConcurrentInvocationCount = new AtomicInteger();
  private final AtomicReference<CronetException> mException = new AtomicReference<>();

  // Set by start() upon success.
  private Map<String, List<String>> mEnvoyRequestHeaders;

  // Pending write data.
  private final ConcurrentLinkedDeque<WriteBuffer> mPendingData;

  // Flush data queue that should be pushed to the native stack when the previous
  // writevData completes.
  private final ConcurrentLinkedDeque<WriteBuffer> mFlushData;

  /* Final metrics recorded the the Envoy Mobile Engine. May be null */
  private EnvoyFinalStreamIntel mEnvoyFinalStreamIntel;

  private volatile WriteBuffer mLastWriteBufferSent;
  private final AtomicReference<ReadBuffer> mLatestBufferRead = new AtomicReference<>();

  // Only modified on the network thread.
  private volatile CronvoyUrlResponseInfoImpl mResponseInfo;

  private Runnable mOnDestroyedCallbackForTesting;

  private final class OnReadCompletedRunnable implements Runnable {
    // Buffer passed back from current invocation of onReadCompleted.
    private ByteBuffer mByteBuffer;
    // End of stream flag from current invocation of onReadCompleted.
    private final boolean mEndOfStream;

    OnReadCompletedRunnable(ByteBuffer mByteBuffer, boolean mEndOfStream) {
      this.mByteBuffer = mByteBuffer;
      this.mEndOfStream = mEndOfStream;
    }

    @Override
    public void run() {
      try {
        // Null out mByteBuffer, to pass buffer ownership to callback or release if done.
        ByteBuffer buffer = mByteBuffer;
        mByteBuffer = null;
        switch (
            mState.nextAction(mEndOfStream ? Event.LAST_READ_COMPLETED : Event.READ_COMPLETED)) {
        case NextAction.NOTIFY_USER_READ_COMPLETED:
          mCallback.onReadCompleted(CronvoyBidirectionalStream.this, mResponseInfo, buffer,
                                    mEndOfStream);
          break;
        case NextAction.TAKE_NO_MORE_ACTIONS:
          // An EM onError callback occurred, or there was a USER_CANCEL event since this task was
          // scheduled.
          return;
        default:
          assert false;
        }
        if (mEndOfStream) {
          switch (mState.nextAction(Event.READY_TO_FINISH)) {
          case NextAction.NOTIFY_USER_SUCCEEDED:
            onSucceededOnExecutor();
            break;
          case NextAction.CARRY_ON:
            break; // Not yet ready to conclude the Stream.
          case NextAction.TAKE_NO_MORE_ACTIONS:
            // Very unlikely: just before this switch statement and after the previous one, an EM
            // onError callback occurred, or there was a USER_CANCEL event.
            return;
          default:
            assert false;
          }
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

        switch (
            mState.nextAction(mEndOfStream ? Event.LAST_WRITE_COMPLETED : Event.WRITE_COMPLETED)) {
        case NextAction.NOTIFY_USER_WRITE_COMPLETED:
          mCallback.onWriteCompleted(CronvoyBidirectionalStream.this, mResponseInfo, buffer,
                                     mEndOfStream);
          break;
        case NextAction.TAKE_NO_MORE_ACTIONS:
          // An EM onError callback occurred, or there was a USER_CANCEL event since this task was
          // scheduled.
          return;
        default:
          assert false;
        }
        if (mEndOfStream) {
          switch (mState.nextAction(Event.READY_TO_FINISH)) {
          case NextAction.NOTIFY_USER_SUCCEEDED:
            onSucceededOnExecutor();
            break;
          case NextAction.CARRY_ON:
            break; // Not yet ready to conclude the Stream.
          case NextAction.TAKE_NO_MORE_ACTIONS:
            // Very unlikely: just before this switch statement and after the previous one, an EM
            // onError callback occurred, or there was a USER_CANCEL event.
          }
        }
      } catch (Exception e) {
        onCallbackException(e);
      }
    }
  }

  CronvoyBidirectionalStream(CronvoyUrlRequestContext requestContext, String url,
                             @CronvoyEngineBase.StreamPriority int priority, Callback callback,
                             Executor executor, String userAgent, String httpMethod,
                             List<Map.Entry<String, String>> requestHeaders,
                             boolean delayRequestHeadersUntilNextFlush,
                             Collection<Object> requestAnnotations, boolean trafficStatsTagSet,
                             int trafficStatsTag, boolean trafficStatsUidSet, int trafficStatsUid) {
    mRequestContext = requestContext;
    mInitialUrl = url;
    mInitialPriority = convertStreamPriority(priority);
    mCallback = new CronvoyVersionSafeCallbacks.BidirectionalStreamCallback(callback);
    mExecutor = executor;
    mUserAgent = userAgent;
    mMethod = httpMethod;
    mRequestHeaders = requestHeaders;
    mDelayRequestHeadersUntilFirstFlush = delayRequestHeadersUntilNextFlush;
    mPendingData = new ConcurrentLinkedDeque<>();
    mFlushData = new ConcurrentLinkedDeque<>();
    mRequestAnnotations = requestAnnotations;
    mTrafficStatsTagSet = trafficStatsTagSet;
    mTrafficStatsTag = trafficStatsTag;
    mTrafficStatsUidSet = trafficStatsUidSet;
    mTrafficStatsUid = trafficStatsUid;
    mReadOnly = !doesMethodAllowWriteData(mMethod);
  }

  @Override
  public void start() {
    validateHttpMethod(mMethod);
    for (Map.Entry<String, String> requestHeader : mRequestHeaders) {
      validateHeader(requestHeader.getKey(), requestHeader.getValue());
    }
    mEnvoyRequestHeaders =
        buildEnvoyRequestHeaders(mMethod, mRequestHeaders, mUserAgent, mInitialUrl);
    // Cronet C++ layer exposes reported errors here with an onError callback. EM does not.
    @Nullable CronetException startUpException = engineSimulatedError(mEnvoyRequestHeaders);
    @Event
    int startingEvent =
        startUpException != null ? Event.ERROR
        : mDelayRequestHeadersUntilFirstFlush
            ? (mReadOnly ? Event.USER_START_READ_ONLY : Event.USER_START)
            : (mReadOnly ? Event.USER_START_WITH_HEADERS_READ_ONLY : Event.USER_START_WITH_HEADERS);
    mRequestContext.onRequestStarted();

    switch (mState.nextAction(startingEvent)) {
    case NextAction.NOTIFY_USER_FAILED:
      mException.set(startUpException);
      failWithException();
      break;
    case NextAction.NOTIFY_USER_STREAM_READY:
      Runnable startTask = new Runnable() {
        @Override
        public void run() {
          try {
            mStream.setStream(mRequestContext.getEnvoyEngine().startStream(
                CronvoyBidirectionalStream.this, /* explicitFlowCrontrol= */ true));
            if (!mDelayRequestHeadersUntilFirstFlush) {
              mStream.sendHeaders(mEnvoyRequestHeaders, mReadOnly);
            }
            onStreamReady();
          } catch (RuntimeException e) {
            // Will be reported when "onCancel" gets invoked.
            reportException(new CronvoyExceptionImpl("Startup failure", e));
          }
        }
      };
      // Starting a new stream can only occur once the engine initialization has completed. The
      // first time a Stream is created this will take more or less 100ms. Keep in mind that Cronet
      // API methods can't be blocking.
      mRequestContext.setTaskToExecuteWhenInitializationIsCompleted(new Runnable() {
        @Override
        public void run() {
          // For the first stream, this task is executed by the Network Thread once the engine
          // initialization is completed. For the subsequent streams, there is no waiting: this line
          // of code is executed by the Thread that invoked this start() method.
          postTaskToExecutor(startTask);
        }
      });
      break;
    default:
      assert false;
    }
  }

  /**
   * Returns, potentially, an exception to be reported through the User's {@link Callback#onFailed},
   * even though no stream has been created yet. This awkward error reporting solely exists to mimic
   * Cronet.
   */
  @Nullable
  private static CronetException engineSimulatedError(Map<String, List<String>> requestHeaders) {
    if (requestHeaders.get(":scheme").get(0).equals("http")) {
      return new CronvoyBidirectionalStreamNetworkException("Exception in BidirectionalStream: "
                                                                + "net::ERR_DISALLOWED_URL_SCHEME",
                                                            11, -301);
    }
    return null;
  }

  @Override
  public void read(ByteBuffer buffer) {
    CronvoyPreconditions.checkHasRemaining(buffer);
    CronvoyPreconditions.checkDirect(buffer);
    mLatestBufferRead.compareAndSet(null, new ReadBuffer(buffer));
    attemptToRead(Event.USER_READ); // Read might not occur right now. If so, it is postponed.
  }

  private void attemptToRead(@Event int readEvent) {
    switch (mState.nextAction(readEvent)) {
    case NextAction.READ: // EM receiving Stream is opened: it accepts "readData" invocations.
      mStream.readData(mLatestBufferRead.get().mByteBuffer.remaining());
      break;
    case NextAction.INVOKE_ON_READ_COMPLETED: // EM receiving Stream is closed.
      // The final read buffer has already been received, or there was no response body.
      ReadBuffer readBuffer = mLatestBufferRead.getAndSet(null);
      onReadCompleted(readBuffer, 0); // Fake the reception of an empty ByteBuffer.
      break;
    case NextAction.POSTPONE_READ: // Response Headers have not yet been received.
    case NextAction.CARRY_ON:      // There was no postponed "read".
      break;
    case NextAction.TAKE_NO_MORE_ACTIONS:
      return;
    default:
      assert false;
    }
  }

  @Override
  public void write(ByteBuffer buffer, boolean endOfStream) {
    CronvoyPreconditions.checkDirect(buffer);
    if (!buffer.hasRemaining() && !endOfStream) {
      throw new IllegalArgumentException("Empty buffer before end of stream.");
    }
    switch (mState.nextAction(endOfStream ? Event.USER_LAST_WRITE : Event.USER_WRITE)) {
    case NextAction.WRITE:
      mPendingData.add(new WriteBuffer(buffer, endOfStream));
      break;
    case NextAction.TAKE_NO_MORE_ACTIONS:
      return;
    default:
      assert false;
    }
  }

  @Override
  public void flush() {
    switch (mState.nextAction(Event.USER_FLUSH)) {
    case NextAction.FLUSH_HEADERS:
      mStream.sendHeaders(mEnvoyRequestHeaders, /* endStream= */ mReadOnly);
      break;
    case NextAction.CARRY_ON:
      break;
    case NextAction.TAKE_NO_MORE_ACTIONS:
      return;
    default:
      assert false;
    }
    if (mUserflushConcurrentInvocationCount.getAndIncrement() > 0) {
      // Another Thread is already copying pending buffers - can't be done concurrently.
      // However, the thread which started with a zero count will loop until this count goes back
      // to zero. For all intent and purposes, this has a similar outcome as using synchronized {}
      return;
    }
    do {
      WriteBuffer pendingBuffer;
      // A write operation can occur while this "flush" method is being executed. This might look
      // like a breach of contract with the Cronet implementation given that this is not possible
      // with Cronet - equivalent code is under a synchronized block. However, for all intents and
      // purposes, this does not affect the general contract: the race condition remains
      // conceptually identical. With Cronet, a distinct Thread invoking a "write" can be lucky or
      // unlucky, depending if that "write" occurred just before the "flush" or not. With Cronvoy,
      // the same "luck" factor is present: it depends if the "write" sent by the other Thread
      // happens before the end of this loop, or not. In short, there is not any strong ordering
      // guarantees between the flush and write when executed by different Threads.
      while ((pendingBuffer = mPendingData.poll()) != null) {
        mFlushData.add(pendingBuffer);
      }
      sendFlushedDataIfAny();
    } while (mUserflushConcurrentInvocationCount.decrementAndGet() > 0);
  }

  private void sendFlushedDataIfAny() {
    if (mFlushConcurrentInvocationCount.getAndIncrement() > 0) {
      // Another Thread is already flushing - can't be done concurrently. However, the thread which
      // started with a zero count will loop until this count goes back to zero. For all intent and
      // purposes, this has a similar outcome as using synchronized {}
      return;
    }
    do {
      if (!mFlushData.isEmpty()) {
        WriteBuffer writeBuffer = mFlushData.getFirst();
        switch (mState.nextAction(writeBuffer.mEndStream ? Event.READY_TO_FLUSH_LAST
                                                         : Event.READY_TO_FLUSH)) {
        case NextAction.SEND_DATA:
          mLastWriteBufferSent = mFlushData.pollFirst();
          mStream.sendData(writeBuffer.mByteBuffer, writeBuffer.mEndStream);
          if (writeBuffer.mEndStream) {
            // There is no EM final callback - last write is therefore acknowledged immediately.
            onWriteCompleted(writeBuffer);
          }
          break;
        case NextAction.CARRY_ON:
          break; // Was not waiting for a "flush" at the moment.
        case NextAction.TAKE_NO_MORE_ACTIONS:
          return;
        default:
          assert false;
        }
      }
    } while (mFlushConcurrentInvocationCount.decrementAndGet() > 0);
  }

  /**
   * Returns a read-only copy of {@code mPendingData} for testing.
   */
  @VisibleForTesting
  public List<ByteBuffer> getPendingDataForTesting() {
    List<ByteBuffer> pendingData = new LinkedList<>();
    for (WriteBuffer writeBuffer : mPendingData) {
      pendingData.add(writeBuffer.mByteBuffer.asReadOnlyBuffer());
    }
    return pendingData;
  }

  /**
   * Returns a read-only copy of {@code mFlushData} for testing.
   *
   * <p>Warning: this does not behave like Cronet. Cronet flushes all buffers in one shot. EM does
   * it one by one.
   */
  @VisibleForTesting
  public List<ByteBuffer> getFlushDataForTesting() {
    List<ByteBuffer> flushData = new LinkedList<>();
    for (WriteBuffer writeBuffer : mFlushData) {
      flushData.add(writeBuffer.mByteBuffer.asReadOnlyBuffer());
    }
    return flushData;
  }

  @Override
  public void cancel() {
    switch (mState.nextAction(Event.USER_CANCEL)) {
    case NextAction.CANCEL:
      mStream.cancel();
      break;
    case NextAction.NOTIFY_USER_CANCELED:
      onCanceledReceived();
      break;
    case NextAction.CARRY_ON:
    case NextAction.TAKE_NO_MORE_ACTIONS:
      // Has already been cancelled, an error condition already registered, or just too late.
      break;
    default:
      assert false;
    }
  }

  @Override
  public boolean isDone() {
    return mState.isDone();
  }

  private void onSucceeded() {
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        onSucceededOnExecutor();
      }
    });
  }

  /**
   * Runs User's {@link Callback#onSucceeded} if both Read and Write sides are closed, and the EM
   * callback {@link #onComplete} was called too.
   */
  private void onSucceededOnExecutor() {
    cleanup();
    try {
      mCallback.onSucceeded(CronvoyBidirectionalStream.this, mResponseInfo);
    } catch (Exception e) {
      Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in onSucceeded method", e);
    }
  }

  private void onStreamReady() {
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        try {
          if (mState.isTerminating()) {
            return;
          }
          mCallback.onStreamReady(CronvoyBidirectionalStream.this);
          // Under duress, or due to user long logic, the response headers might have been received
          // already. In that case mCallback.onResponseHeadersReceived was purposely not called, and
          // therefore this is done here. This guarantees correct ordering: mCallback.onStreamReady
          // must finish before invoking mCallback.onResponseHeadersReceived.
          switch (mState.nextAction(Event.STREAM_READY_CALLBACK_DONE)) {
          case NextAction.NOTIFY_USER_HEADERS_RECEIVED:
            mCallback.onResponseHeadersReceived(CronvoyBidirectionalStream.this, mResponseInfo);
            break;
          case NextAction.CARRY_ON:
            break; // Response headers have not been received yet - most common outcome.
          case NextAction.TAKE_NO_MORE_ACTIONS:
            return;
          default:
            assert false;
          }
        } catch (Exception e) {
          onCallbackException(e);
        }
      }
    });
  }

  /**
   * Called when the response headers are received.
   *
   * <p>Note: If the User's {@link Callback#onStreamReady} method has not yet finished, then this
   * method won't be invoked - User's {@link Callback#onResponseHeadersReceived} method will instead
   * be invoked just after {@link Callback#onStreamReady} completion. See method above.
   */
  private void onResponseHeadersReceived() {
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        try {
          if (mState.isTerminating()) {
            return;
          }
          mCallback.onResponseHeadersReceived(CronvoyBidirectionalStream.this, mResponseInfo);
        } catch (Exception e) {
          onCallbackException(e);
        }
      }
    });
  }

  private void onReadCompleted(ReadBuffer readBuffer, int bytesRead) {
    ByteBuffer byteBuffer = readBuffer.mByteBuffer;
    int initialPosition = readBuffer.mInitialPosition;
    int initialLimit = readBuffer.mInitialLimit;
    if (byteBuffer.position() != initialPosition || byteBuffer.limit() != initialLimit) {
      reportException(new CronvoyExceptionImpl("ByteBuffer modified externally during read", null));
      return;
    }
    if (bytesRead < 0 || initialPosition + bytesRead > initialLimit) {
      reportException(new CronvoyExceptionImpl("Invalid number of bytes read", null));
      return;
    }
    byteBuffer.position(initialPosition + bytesRead);
    postTaskToExecutor(new OnReadCompletedRunnable(byteBuffer, bytesRead == 0));
  }

  private void onWriteCompleted(WriteBuffer writeBuffer) {
    ByteBuffer buffer = writeBuffer.mByteBuffer;
    if (buffer.position() != writeBuffer.mInitialPosition ||
        buffer.limit() != writeBuffer.mInitialLimit) {
      reportException(
          new CronvoyExceptionImpl("ByteBuffer modified externally during write", null));
      return;
    }
    // Current implementation always writes the complete buffer.
    buffer.position(buffer.limit());
    postTaskToExecutor(new OnWriteCompletedRunnable(buffer, writeBuffer.mEndStream));
  }

  private void onResponseTrailersReceived(List<Map.Entry<String, String>> trailers) {
    final UrlResponseInfo.HeaderBlock trailersBlock = new HeaderBlockImpl(trailers);
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        try {
          if (mState.isTerminating()) {
            return;
          }
          mCallback.onResponseTrailersReceived(CronvoyBidirectionalStream.this, mResponseInfo,
                                               trailersBlock);
        } catch (Exception e) {
          onCallbackException(e);
        }
      }
    });
  }

  private void onErrorReceived(int errorCode, String message,
                               EnvoyFinalStreamIntel finalStreamIntel) {
    if (mResponseInfo != null) {
      mResponseInfo.setReceivedByteCount(finalStreamIntel.getReceivedByteCount());
    }

    NetError netError = mapEnvoyMobileErrorToNetError(finalStreamIntel);
    int javaError = mapNetErrorToCronetApiErrorCode(netError);

    if (isQuicException(javaError)) {
      // `message` is populated from StreamInfo::responseCodeDetails(), so `message` is used to
      // populate the error details in the exception.
      mException.set(new CronvoyQuicExceptionImpl("Exception in BidirectionalStream: " + netError,
                                                  javaError, netError.getErrorCode(),
                                                  Errors.QUIC_INTERNAL_ERROR, message));
    } else {
      // `message` is populated from StreamInfo::responseCodeDetails(), so `message` is used to
      // populate the error details in the exception.
      mException.set(new CronvoyBidirectionalStreamNetworkException(
          "Exception in BidirectionalStream: " + netError, javaError, netError.getErrorCode(),
          message));
    }

    failWithException();
  }

  /**
   * Called when request is canceled, no callbacks will be called afterwards.
   */
  private void onCanceledReceived() {
    cleanup();
    postTaskToExecutor(new Runnable() {
      @Override
      public void run() {
        try {
          mCallback.onCanceled(CronvoyBidirectionalStream.this, mResponseInfo);
        } catch (Exception e) {
          Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in onCanceled method", e);
        }
      }
    });
  }

  /**
   * Report metrics to listeners.
   */
  private void onMetricsCollected(long requestStartMs, long dnsStartMs, long dnsEndMs,
                                  long connectStartMs, long connectEndMs, long sslStartMs,
                                  long sslEndMs, long sendingStartMs, long sendingEndMs,
                                  long pushStartMs, long pushEndMs, long responseStartMs,
                                  long requestEndMs, boolean socketReused, long sentByteCount,
                                  long receivedByteCount) {
    // Metrics information. Obtained when request succeeds, fails or is canceled.
    RequestFinishedInfo.Metrics mMetrics = new CronvoyMetrics(
        requestStartMs, dnsStartMs, dnsEndMs, connectStartMs, connectEndMs, sslStartMs, sslEndMs,
        sendingStartMs, sendingEndMs, pushStartMs, pushEndMs, responseStartMs, requestEndMs,
        socketReused, sentByteCount, receivedByteCount);
    final RequestFinishedInfo requestFinishedInfo = new CronvoyRequestFinishedInfoImpl(
        mInitialUrl, mRequestAnnotations, mMetrics, mState.getFinishedReason(), mResponseInfo,
        mException.get());
    mRequestContext.reportRequestFinished(requestFinishedInfo);
  }

  @VisibleForTesting
  public void setOnDestroyedCallbackForTesting(Runnable onDestroyedCallbackForTesting) {
    mOnDestroyedCallbackForTesting = onDestroyedCallbackForTesting;
  }

  private static boolean doesMethodAllowWriteData(String methodName) {
    return !methodName.equals("GET") && !methodName.equals("HEAD");
  }

  private static int convertStreamPriority(@CronvoyEngineBase.StreamPriority int priority) {
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
      Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception posting task to executor", failException);
      // If already in a failed state this invocation is a no-op.
      reportException(
          new CronvoyExceptionImpl("Exception posting task to executor", failException));
    }
  }

  private CronvoyUrlResponseInfoImpl
  prepareResponseInfoOnNetworkThread(int httpStatusCode, String negotiatedProtocol,
                                     Map<String, List<String>> responseHeaders,
                                     long receivedByteCount) {
    List<Map.Entry<String, String>> headers = new ArrayList<>();
    for (Map.Entry<String, List<String>> headerEntry : responseHeaders.entrySet()) {
      String headerKey = headerEntry.getKey();
      if (headerEntry.getValue().get(0) == null) {
        continue;
      }
      if (!headerKey.startsWith(X_ENVOY) && !headerKey.equals("date")) {
        for (String value : headerEntry.getValue()) {
          headers.add(new AbstractMap.SimpleEntry<>(headerKey, value));
        }
      }
    }
    // proxy and caching are not supported.
    CronvoyUrlResponseInfoImpl responseInfo =
        new CronvoyUrlResponseInfoImpl(Collections.singletonList(mInitialUrl), httpStatusCode, "",
                                       headers, false, negotiatedProtocol, null, receivedByteCount);
    return responseInfo;
  }

  private void cleanup() {
    if (mEnvoyFinalStreamIntel != null) {
      recordFinalIntel(mEnvoyFinalStreamIntel);
    }
    mRequestContext.onRequestDestroyed();
    if (mOnDestroyedCallbackForTesting != null) {
      mOnDestroyedCallbackForTesting.run();
    }
  }

  /**
   * Fails the stream with an exception.
   */
  private void failWithException() {
    assert mException.get() != null;
    cleanup();
    mExecutor.execute(new Runnable() {
      @Override
      public void run() {
        try {
          mCallback.onFailed(CronvoyBidirectionalStream.this, mResponseInfo, mException.get());
        } catch (Exception failException) {
          Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception notifying of failed request",
                failException);
        }
      }
    });
  }

  /**
   * If callback method throws an exception, stream gets canceled and exception is reported via
   * User's {@link Callback#onFailed}.
   */
  private void onCallbackException(Exception e) {
    CallbackException streamError =
        new CronvoyCallbackExceptionImpl("CalledByNative method has thrown an exception", e);
    Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in CalledByNative method", e);
    reportException(streamError);
  }

  /**
   * Reports an exception. Can be called on any thread. Only the first call is recorded. The
   * User's {@link Callback#onFailed} will be scheduled not before any of the final EM callback has
   * been invoked ({@link #onCancel}, {@link #onComplete}, or {@link #onError}).
   */
  private void reportException(CronetException exception) {
    mException.compareAndSet(null, exception);
    switch (mState.nextAction(Event.ERROR)) {
    case NextAction.CANCEL:
      mStream.cancel();
      break;
    case NextAction.NOTIFY_USER_FAILED:
      failWithException();
      break;
    case NextAction.TAKE_NO_MORE_ACTIONS:
      Log.e(CronvoyUrlRequestContext.LOG_TAG,
            "An exception has already been previously recorded. This one is ignored.", exception);
      return;
    default:
      assert false;
    }
  }

  private void recordFinalIntel(EnvoyFinalStreamIntel intel) {
    if (mRequestContext.hasRequestFinishedListener()) {
      onMetricsCollected(intel.getStreamStartMs(), intel.getDnsStartMs(), intel.getDnsEndMs(),
                         intel.getConnectStartMs(), intel.getConnectEndMs(), intel.getSslStartMs(),
                         intel.getSslEndMs(), intel.getSendingStartMs(), intel.getSendingEndMs(),
                         /* pushStartMs= */ -1, /* pushEndMs= */ -1, intel.getResponseStartMs(),
                         intel.getStreamEndMs(), intel.getSocketReused(), intel.getSentByteCount(),
                         intel.getReceivedByteCount());
    }
  }

  private static void validateHttpMethod(String method) {
    if (method == null) {
      throw new NullPointerException("Method is required.");
    }
    if ("OPTIONS".equalsIgnoreCase(method) || "GET".equalsIgnoreCase(method) ||
        "HEAD".equalsIgnoreCase(method) || "POST".equalsIgnoreCase(method) ||
        "PUT".equalsIgnoreCase(method) || "DELETE".equalsIgnoreCase(method) ||
        "TRACE".equalsIgnoreCase(method) || "PATCH".equalsIgnoreCase(method)) {
      return;
    }
    throw new IllegalArgumentException("Invalid http method " + method);
  }

  private static void validateHeader(String header, String value) {
    if (header == null) {
      throw new NullPointerException("Invalid header name.");
    }
    if (value == null) {
      throw new NullPointerException("Invalid header value.");
    }
    if (!isValidHeaderName(header) || value.contains("\r\n")) {
      throw new IllegalArgumentException("Invalid header " + header + "=" + value);
    }
  }

  private static boolean isValidHeaderName(String header) {
    for (int i = 0; i < header.length(); i++) {
      char c = header.charAt(i);
      switch (c) {
      case '(':
      case ')':
      case '<':
      case '>':
      case '@':
      case ',':
      case ';':
      case ':':
      case '\\':
      case '\'':
      case '/':
      case '[':
      case ']':
      case '?':
      case '=':
      case '{':
      case '}':
        return false;
      default: {
        if (Character.isISOControl(c) || Character.isWhitespace(c)) {
          return false;
        }
      }
      }
    }
    return true;
  }

  private static Map<String, List<String>>
  buildEnvoyRequestHeaders(String initialMethod, List<Map.Entry<String, String>> headerList,
                           String userAgent, String currentUrl) {
    Map<String, List<String>> headers = new LinkedHashMap<>();
    final URL url;
    try {
      url = new URL(currentUrl);
    } catch (MalformedURLException e) {
      throw new IllegalArgumentException("Invalid URL", e);
    }
    // TODO: with an empty string it does not always work. Why?
    String path = url.getFile().isEmpty() ? "/" : url.getFile();
    headers.computeIfAbsent(":authority", unused -> new ArrayList<>()).add(url.getAuthority());
    headers.computeIfAbsent(":method", unused -> new ArrayList<>()).add(initialMethod);
    headers.computeIfAbsent(":path", unused -> new ArrayList<>()).add(path);
    headers.computeIfAbsent(":scheme", unused -> new ArrayList<>()).add(url.getProtocol());
    boolean hasUserAgent = false;
    for (Map.Entry<String, String> header : headerList) {
      if (header.getKey().isEmpty()) {
        throw new IllegalArgumentException("Invalid header =");
      }
      hasUserAgent = hasUserAgent ||
                     (header.getKey().equalsIgnoreCase(USER_AGENT) && !header.getValue().isEmpty());
      headers.computeIfAbsent(header.getKey(), unused -> new ArrayList<>()).add(header.getValue());
    }
    if (!hasUserAgent) {
      headers.computeIfAbsent(USER_AGENT, unused -> new ArrayList<>()).add(userAgent);
    }
    return headers;
  }

  @Override
  public void onSendWindowAvailable(EnvoyStreamIntel streamIntel) {
    switch (mState.nextAction(Event.ON_SEND_WINDOW_AVAILABLE)) {
    case NextAction.CHAIN_NEXT_WRITE:
      onWriteCompleted(mLastWriteBufferSent);
      sendFlushedDataIfAny(); // Flush if there is anything in the flush queue mFlushData.
      break;
    case NextAction.TAKE_NO_MORE_ACTIONS:
      return;
    default:
      assert false;
    }
  }

  @Override
  public void onHeaders(Map<String, List<String>> headers, boolean endStream,
                        EnvoyStreamIntel streamIntel) {
    List<String> statuses = headers.get(":status");
    int httpStatusCode =
        statuses != null && !statuses.isEmpty() ? Integer.parseInt(statuses.get(0)) : -1;
    List<String> transportValues = headers.get(X_ENVOY_SELECTED_TRANSPORT);
    String negotiatedProtocol =
        transportValues != null && !transportValues.isEmpty() ? transportValues.get(0) : "unknown";
    try {
      mResponseInfo = prepareResponseInfoOnNetworkThread(
          httpStatusCode, negotiatedProtocol, headers, streamIntel.getConsumedBytesFromResponse());
    } catch (Exception e) {
      reportException(new CronvoyExceptionImpl("Cannot prepare ResponseInfo", null));
      return;
    }

    switch (mState.nextAction(endStream ? Event.ON_HEADERS_END_STREAM : Event.ON_HEADERS)) {
    case NextAction.NOTIFY_USER_HEADERS_RECEIVED:
      onResponseHeadersReceived();
      break;
    case NextAction.CARRY_ON:
      break; // User has not finished executing the "streamReady" callback - must wait.
    case NextAction.TAKE_NO_MORE_ACTIONS:
      return;
    default:
      assert false;
    }

    attemptToRead(Event.READY_TO_START_POSTPONED_READ_IF_ANY);
  }

  @Override
  public void onData(ByteBuffer data, boolean endStream, EnvoyStreamIntel streamIntel) {
    mResponseInfo.setReceivedByteCount(streamIntel.getConsumedBytesFromResponse());
    switch (mState.nextAction(endStream ? Event.ON_DATA_END_STREAM : Event.ON_DATA)) {
    case NextAction.INVOKE_ON_READ_COMPLETED:
      ReadBuffer readBuffer = mLatestBufferRead.getAndSet(null);
      ByteBuffer userBuffer = readBuffer.mByteBuffer;
      // TODO: this copies buffer on the Network Thread - consider doing on the user Thread.
      //       Or even better, revamp EM API to avoid as much as possible copying ByteBuffers.
      userBuffer.mark();
      userBuffer.put(data); // NPE ==> BUG, BufferOverflowException ==> User not behaving.
      userBuffer.reset();
      onReadCompleted(readBuffer, data.capacity());
      break;
    case NextAction.TAKE_NO_MORE_ACTIONS:
      return;
    default:
      assert false;
    }
  }

  @Override
  public void onTrailers(Map<String, List<String>> trailers, EnvoyStreamIntel streamIntel) {
    List<Map.Entry<String, String>> headers = new ArrayList<>();
    switch (mState.nextAction(Event.ON_TRAILERS)) {
    case NextAction.NOTIFY_USER_TRAILERS_RECEIVED:
      for (Map.Entry<String, List<String>> headerEntry : trailers.entrySet()) {
        String headerKey = headerEntry.getKey();
        if (headerEntry.getValue().get(0) == null) {
          continue;
        }
        // TODO: make sure which headers should be posted.
        if (!headerKey.startsWith(X_ENVOY) && !headerKey.equals("date") &&
            !headerKey.startsWith(":")) {
          for (String value : headerEntry.getValue()) {
            headers.add(new AbstractMap.SimpleEntry<>(headerKey, value));
          }
        }
      }
      onResponseTrailersReceived(headers);
      break;
    case NextAction.TAKE_NO_MORE_ACTIONS:
      return;
    default:
      assert false;
    }
  }

  @Override
  public void onError(int errorCode, String message, int attemptCount, EnvoyStreamIntel streamIntel,
                      EnvoyFinalStreamIntel finalStreamIntel) {
    mEnvoyFinalStreamIntel = finalStreamIntel;
    switch (mState.nextAction(Event.ON_ERROR)) {
    case NextAction.NOTIFY_USER_NETWORK_ERROR:
      onErrorReceived(errorCode, message, finalStreamIntel);
      break;
    case NextAction.NOTIFY_USER_FAILED:
      // There was already an error in-progress - the network error came too late and is ignored.
      failWithException();
      break;
    default:
      assert false;
    }
  }

  @Override
  public void onCancel(EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel) {
    mEnvoyFinalStreamIntel = finalStreamIntel;
    switch (mState.nextAction(Event.ON_CANCEL)) {
    case NextAction.NOTIFY_USER_CANCELED:
      onCanceledReceived(); // The cancel was user initiated.
      break;
    case NextAction.NOTIFY_USER_FAILED:
      failWithException(); // The cancel was not user initiated, but a mean to report the error.
      break;
    default:
      assert false;
    }
  }

  @Override
  public void onComplete(EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel) {
    mEnvoyFinalStreamIntel = finalStreamIntel;
    switch (mState.nextAction(Event.ON_COMPLETE)) {
    case NextAction.NOTIFY_USER_FAILED:
      failWithException();
      break;
    case NextAction.NOTIFY_USER_CANCELED:
      onCanceledReceived();
      break;
    case NextAction.NOTIFY_USER_SUCCEEDED:
      onSucceeded();
      break;
    case NextAction.CARRY_ON:
      break;
    default:
      assert false;
    }
  }

  private static class WriteBuffer {
    final ByteBuffer mByteBuffer;
    final boolean mEndStream;
    final int mInitialPosition;
    final int mInitialLimit;

    WriteBuffer(ByteBuffer mByteBuffer, boolean mEndStream) {
      this.mByteBuffer = mByteBuffer;
      this.mEndStream = mEndStream;
      this.mInitialPosition = mByteBuffer.position();
      this.mInitialLimit = mByteBuffer.limit();
    }
  }

  private static class ReadBuffer {
    final ByteBuffer mByteBuffer;
    final int mInitialPosition;
    final int mInitialLimit;

    ReadBuffer(ByteBuffer mByteBuffer) {
      this.mByteBuffer = mByteBuffer;
      this.mInitialPosition = mByteBuffer.position();
      this.mInitialLimit = mByteBuffer.limit();
    }
  }
}
