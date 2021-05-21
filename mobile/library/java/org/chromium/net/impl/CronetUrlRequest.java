package org.chromium.net.impl;

import android.annotation.TargetApi;
import android.net.TrafficStats;
import android.os.Build;
import android.util.Log;
import androidx.annotation.GuardedBy;
import androidx.annotation.IntDef;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.UpstreamHttpProtocol;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.net.URI;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.AbstractMap.SimpleEntry;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;
import org.chromium.net.CronetException;
import org.chromium.net.InlineExecutionProhibitedException;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UrlResponseInfo;
import org.chromium.net.impl.Executors.CheckedRunnable;
import org.chromium.net.impl.Executors.DirectPreventingExecutor;

/** UrlRequest, backed by Envoy-Mobile. */
@TargetApi(Build.VERSION_CODES.ICE_CREAM_SANDWICH) // TrafficStats only available on ICS
final class CronetUrlRequest extends UrlRequestBase {

  /**
   * State interface for keeping track of the internal state of a {@link UrlRequestBase}.
   * <pre>
   *               /- AWAITING_FOLLOW_REDIRECT <- REDIRECT_RECEIVED <-\     /- READING <--\
   *               |                                                  |     |             |
   *               V                                                  /     V             /
   * NOT_STARTED -> STARTED -----------------------------------------------> AWAITING_READ -------
   * --> COMPLETE
   * </pre>
   */
  @IntDef({State.NOT_STARTED, State.STARTED, State.REDIRECT_RECEIVED,
           State.AWAITING_FOLLOW_REDIRECT, State.AWAITING_READ, State.READING, State.ERROR,
           State.COMPLETE, State.CANCELLED})
  @Retention(RetentionPolicy.SOURCE)
  @interface State {
    int NOT_STARTED = 0;
    int STARTED = 1;
    int REDIRECT_RECEIVED = 2;
    int AWAITING_FOLLOW_REDIRECT = 3;
    int AWAITING_READ = 4;
    int READING = 5;
    int ERROR = 6;
    int COMPLETE = 7;
    int CANCELLED = 8;
  }

  private static final String X_ANDROID = "X-Android";
  private static final String X_ANDROID_SELECTED_TRANSPORT = "X-Android-Selected-Transport";
  private static final String TAG = CronetUrlRequest.class.getSimpleName();
  private static final String USER_AGENT = "User-Agent";

  private final AsyncUrlRequestCallback mCallbackAsync;
  private final PausableSerializingExecutor mCronvoyExecutor;
  private final String mUserAgent;
  private final Map<String, String> mRequestHeaders = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
  private final List<String> mUrlChain = new ArrayList<>();
  private final CronetUrlRequestContext mCronvoyEngine;

  /**
   * This is the source of thread safety in this class - no other synchronization is performed. By
   * compare-and-swapping from one state to another, we guarantee that operations aren't running
   * concurrently. Only the winner of a compare-and-swapping proceeds.
   *
   * <p>A caller can lose a compare-and-swapping for three reasons - user error (two calls to read()
   * without waiting for the read to succeed), runtime error (network code or user code throws an
   * exception), or cancellation.
   */
  private final AtomicInteger /* State */ mState = new AtomicInteger(State.NOT_STARTED);

  private final AtomicBoolean mUploadProviderClosed = new AtomicBoolean(false);

  private final boolean mAllowDirectExecutor;

  /* These don't change with redirects */
  private String mInitialMethod;
  private VersionSafeCallbacks.UploadDataProviderWrapper mUploadDataProvider;
  private Executor mUploadExecutor;
  private boolean mEndStream;
  private final AtomicBoolean mCancelCalled = new AtomicBoolean();
  private final AtomicReference<ByteBuffer> mMostRecentBufferRead = new AtomicReference<>();
  private final AtomicReference<ByteBuffer> mUserCurrentReadBuffer = new AtomicReference<>();
  private final Supplier<PausableSerializingExecutor> mEnvoyCallbackExecutorSupplier;

  /**
   * Holds a subset of StatusValues - {@link State#STARTED} can represent {@link
   * Status#SENDING_REQUEST} or {@link Status#WAITING_FOR_RESPONSE}. While the distinction isn't
   * needed to implement the logic in this class, it is needed to implement {@link
   * #getStatus(StatusListener)}.
   *
   * <p>Concurrency notes - this value is not atomically updated with state, so there is some risk
   * that we'd get an inconsistent snapshot of both - however, it also happens that this value is
   * only used with the STARTED state, so it's inconsequential.
   */
  @StatusValues private volatile int mAdditionalStatusDetails = Status.INVALID;

  /* These change with redirects. */
  private Stream mStream;
  private PausableSerializingExecutor mEnvoyCallbackExecutor;
  private String mCurrentUrl;
  private UrlResponseInfoImpl mUrlResponseInfo;
  private String mPendingRedirectUrl;
  private OutputStreamDataSink mOutputStreamDataSink;

  /**
   * @param executor The executor for orchestrating tasks between envoy-mobile callbacks
   * @param userExecutor The executor used to dispatch to Cronet {@code callback}
   */
  CronetUrlRequest(CronetUrlRequestContext cronvoyEngine, Callback callback,
                   final Executor executor, Executor userExecutor, String url, String userAgent,
                   boolean allowDirectExecutor, boolean trafficStatsTagSet, int trafficStatsTag,
                   final boolean trafficStatsUidSet, final int trafficStatsUid) {
    if (url == null) {
      throw new NullPointerException("URL is required");
    }
    if (callback == null) {
      throw new NullPointerException("Listener is required");
    }
    if (executor == null) {
      throw new NullPointerException("Executor is required");
    }
    if (userExecutor == null) {
      throw new NullPointerException("userExecutor is required");
    }

    mCronvoyEngine = cronvoyEngine;
    mAllowDirectExecutor = allowDirectExecutor;
    mCallbackAsync = new AsyncUrlRequestCallback(callback, userExecutor);
    final int trafficStatsTagToUse =
        trafficStatsTagSet ? trafficStatsTag : TrafficStats.getThreadStatsTag();
    mCronvoyExecutor = createSerializedExecutor(executor, trafficStatsUidSet, trafficStatsUid,
                                                trafficStatsTagToUse);
    mEnvoyCallbackExecutorSupplier = ()
        -> createSerializedExecutor(executor, trafficStatsUidSet, trafficStatsUid,
                                    trafficStatsTagToUse);
    mCurrentUrl = url;
    mUserAgent = userAgent;
  }

  private static PausableSerializingExecutor createSerializedExecutor(Executor executor,
                                                                      boolean trafficStatsUidSet,
                                                                      int trafficStatsUid,
                                                                      int trafficStatsTagToUse) {
    return new PausableSerializingExecutor(command -> executor.execute(() -> {
      int oldTag = TrafficStats.getThreadStatsTag();
      TrafficStats.setThreadStatsTag(trafficStatsTagToUse);
      if (trafficStatsUidSet) {
        ThreadStatsUid.set(trafficStatsUid);
      }
      try {
        command.run();
      } finally {
        if (trafficStatsUidSet) {
          ThreadStatsUid.clear();
        }
        TrafficStats.setThreadStatsTag(oldTag);
      }
    }));
  }

  @Override
  public void setHttpMethod(String method) {
    checkNotStarted();
    if (method == null) {
      throw new NullPointerException("Method is required.");
    }
    if ("OPTIONS".equalsIgnoreCase(method) || "GET".equalsIgnoreCase(method) ||
        "HEAD".equalsIgnoreCase(method) || "POST".equalsIgnoreCase(method) ||
        "PUT".equalsIgnoreCase(method) || "DELETE".equalsIgnoreCase(method) ||
        "TRACE".equalsIgnoreCase(method) || "PATCH".equalsIgnoreCase(method)) {
      mInitialMethod = method;
    } else {
      throw new IllegalArgumentException("Invalid http method " + method);
    }
  }

  private void checkNotStarted() {
    @State int state = mState.get();
    if (state != State.NOT_STARTED) {
      throw new IllegalStateException("Request is already started. State is: " + state);
    }
  }

  @Override
  public void addHeader(String header, String value) {
    checkNotStarted();
    if (!isValidHeaderName(header) || value.contains("\r\n")) {
      throw new IllegalArgumentException("Invalid header " + header + "=" + value);
    }
    mRequestHeaders.put(header, value);
  }

  private boolean isValidHeaderName(String header) {
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

  @Override
  public void setUploadDataProvider(UploadDataProvider uploadDataProvider, Executor executor) {
    if (uploadDataProvider == null) {
      throw new NullPointerException("Invalid UploadDataProvider.");
    }
    if (!mRequestHeaders.containsKey("Content-Type")) {
      throw new IllegalArgumentException("Requests with upload data must have a Content-Type.");
    }
    checkNotStarted();
    if (mInitialMethod == null) {
      mInitialMethod = "POST";
    }
    mUploadDataProvider = new VersionSafeCallbacks.UploadDataProviderWrapper(uploadDataProvider);
    if (mAllowDirectExecutor) {
      mUploadExecutor = executor;
    } else {
      mUploadExecutor = new DirectPreventingExecutor(executor);
    }
  }

  private final class OutputStreamDataSink extends CronetUploadDataStream {

    OutputStreamDataSink() { super(mUploadExecutor, mCronvoyExecutor, mUploadDataProvider); }

    @Override
    protected void finish() {}

    @Override
    protected int processSuccessfulRead(ByteBuffer buffer, boolean finalChunk) {
      if (buffer.capacity() != buffer.remaining()) {
        // Unfortunately, Envoy-Mobile does not care about the buffer limit - buffer must get
        // copied to the correct size.
        buffer = ByteBuffer.allocateDirect(buffer.remaining()).put(buffer);
      }
      if (finalChunk) {
        mStream.close(buffer);
      } else {
        mStream.sendData(buffer);
      }
      return buffer.capacity();
    }

    @Override
    protected Runnable getErrorSettingRunnable(CheckedRunnable runnable) {
      return errorSetting(runnable);
    }

    @Override
    protected Runnable getUploadErrorSettingRunnable(CheckedRunnable runnable) {
      return uploadErrorSetting(runnable);
    }

    @Override
    protected void processUploadError(Throwable exception) {
      enterUploadErrorState(exception);
    }
  }

  @Override
  public void start() {
    transitionStates(State.NOT_STARTED, State.STARTED, () -> {
      mCronvoyExecutor.pause();
      mCronvoyEngine.setTaskToExecuteWhenInitializationIsCompleted(mCronvoyExecutor::resume);
      mAdditionalStatusDetails = Status.CONNECTING;
      mUrlChain.add(mCurrentUrl);
      fireOpenConnection();
    });
  }

  private void enterErrorState(final CronetException error) {
    if (setTerminalState(State.ERROR)) {
      if (mCancelCalled.compareAndSet(false, true)) {
        mStream.cancel();
      }
      fireCloseUploadDataProvider();
      mCallbackAsync.onFailed(mUrlResponseInfo, error);
    }
  }

  private boolean setTerminalState(@State int error) {
    while (true) {
      @State int oldState = mState.get();
      switch (oldState) {
      case State.NOT_STARTED:
        throw new IllegalStateException("Can't enter error state before start");
      case State.ERROR:    // fallthrough
      case State.COMPLETE: // fallthrough
      case State.CANCELLED:
        return false; // Already in a terminal state
      default: {
        if (mState.compareAndSet(/* expect= */ oldState, /* update= */ error)) {
          return true;
        }
      }
      }
    }
  }

  /** Ends the request with an error, caused by an exception thrown from user code. */
  private void enterUserErrorState(final Throwable error) {
    enterErrorState(
        new CallbackExceptionImpl("Exception received from UrlRequest.Callback", error));
  }

  /** Ends the request with an error, caused by an exception thrown from user code. */
  private void enterUploadErrorState(final Throwable error) {
    enterErrorState(new CallbackExceptionImpl("Exception received from UploadDataProvider", error));
  }

  private void enterCronetErrorState(final Throwable error) {
    enterErrorState(new CronetExceptionImpl("System error", error));
  }

  /**
   * Atomically swaps from the expected state to a new state. If the swap fails, and it's not due to
   * an earlier error or cancellation, throws an exception.
   *
   * @param afterTransition Callback to run after transition completes successfully.
   */
  private void transitionStates(@State int expected, @State int newState,
                                Runnable afterTransition) {
    if (!mState.compareAndSet(expected, newState)) {
      @State int state = mState.get();
      if (!(state == State.CANCELLED || state == State.ERROR)) {
        throw new IllegalStateException("Invalid state transition - expected " + expected +
                                        " but was " + state);
      }
    } else {
      afterTransition.run();
    }
  }

  @Override
  public void followRedirect() {
    transitionStates(State.AWAITING_FOLLOW_REDIRECT, State.STARTED, () -> {
      mCurrentUrl = mPendingRedirectUrl;
      mPendingRedirectUrl = null;
      fireOpenConnection();
    });
  }

  private void onResponseHeaders(ResponseHeaders responseHeaders, boolean lastCallback) {
    mAdditionalStatusDetails = Status.WAITING_FOR_RESPONSE;
    if (mState.get() == State.CANCELLED) {
      return;
    }
    final List<Map.Entry<String, String>> headerList = new ArrayList<>();
    String selectedTransport = "http/1.1"; // TODO(carloseltuerto) looks dubious
    Set<Map.Entry<String, List<String>>> headers = responseHeaders.allHeaders().entrySet();

    for (Map.Entry<String, List<String>> headerEntry : headers) {
      String headerKey = headerEntry.getKey();
      String value = headerEntry.getValue().get(0);
      if (value == null) {
        continue;
      }
      if (X_ANDROID_SELECTED_TRANSPORT.equalsIgnoreCase(headerKey)) {
        selectedTransport = value;
      }
      if (!headerKey.startsWith(X_ANDROID)) {
        headerList.add(new SimpleEntry<>(headerKey, value));
      }
    }
    int responseCode =
        responseHeaders.getHttpStatus() == null ? -1 : responseHeaders.getHttpStatus();
    // Important to copy the list here, because although we never concurrently modify
    // the list ourselves, user code might iterate over it while we're redirecting, and
    // that would throw ConcurrentModificationException.
    // TODO(https://github.com/envoyproxy/envoy-mobile/issues/1426) set receivedByteCount
    mUrlResponseInfo = new UrlResponseInfoImpl(
        new ArrayList<>(mUrlChain), responseCode,
        "HTTP " + responseHeaders.getHttpStatus(), // UrlConnection.getResponseMessage(),
        Collections.unmodifiableList(headerList), false, selectedTransport, "", 0);
    if (responseCode >= 300 && responseCode < 400) {
      List<String> locationFields = mUrlResponseInfo.getAllHeaders().get("location");
      if (locationFields != null) {
        if (!lastCallback) {
          mStream.cancel(); // This is not technically needed.
          // This deals with unwanted "setOnResponseData" callbacks. By API contract, response body
          // on a redirect is to be silently ignored.
          mEnvoyCallbackExecutor.shutdown();
        }
        fireRedirectReceived(locationFields.get(0));
        return;
      }
    }
    fireCloseUploadDataProvider();
    mEndStream = lastCallback;
    // There is no "body" data: fake an empty response to trigger the Cronet next step.
    if (mEndStream) {
      // By contract, envoy-mobile won't send more "callbacks".
      mMostRecentBufferRead.set(ByteBuffer.allocateDirect(0));
    }
    mCallbackAsync.onResponseStarted(mUrlResponseInfo);
  }

  private void fireCloseUploadDataProvider() {
    if (mUploadDataProvider != null &&
        mUploadProviderClosed.compareAndSet(/* expect= */ false, /* update= */ true)) {
      try {
        mUploadExecutor.execute(uploadErrorSetting(mUploadDataProvider::close));
      } catch (RejectedExecutionException e) {
        Log.e(TAG, "Exception when closing uploadDataProvider", e);
      }
    }
  }

  private void fireRedirectReceived(final String locationField) {
    transitionStates(State.STARTED, State.REDIRECT_RECEIVED, () -> {
      mPendingRedirectUrl = URI.create(mCurrentUrl).resolve(locationField).toString();
      mUrlChain.add(mPendingRedirectUrl);
      transitionStates(
          State.REDIRECT_RECEIVED, State.AWAITING_FOLLOW_REDIRECT,
          () -> mCallbackAsync.onRedirectReceived(mUrlResponseInfo, mPendingRedirectUrl));
    });
  }

  private void fireOpenConnection() {
    // The envoyCallbackExecutor is tied to the life cycle of the stream. If the stream is not
    // useful anymore, so is the envoyCallbackExecutor. Only the stream can schedule tasks through
    // that executor - this is done with the "callbacks" below.
    mEnvoyCallbackExecutor = mEnvoyCallbackExecutorSupplier.get(); // get() creates a new instance.
    mCronvoyExecutor.execute(errorSetting(() -> {
      // If we're cancelled, then our old connection will be disconnected for us and
      // we shouldn't open a new one.
      if (mState.get() == State.CANCELLED) {
        return;
      }
      if (mInitialMethod == null) {
        mInitialMethod = RequestMethod.GET.name();
      }
      boolean isHttp2Enabled = mCronvoyEngine.getBuilder().http2Enabled();
      URL url = new URL(mCurrentUrl);
      RequestHeaders envoyRequestHeaders = buildEnvoyRequestHeaders(
          url, isHttp2Enabled, mInitialMethod, mRequestHeaders, mUserAgent);

      // Note: none of these "callbacks" are getting executed immediately. The envoyCallbackExecutor
      // is in reality a task scheduler. The execution of these tasks are serialized - concurrency
      // issues should not be a concern here.
      mStream = mCronvoyEngine.getEnvoyEngine()
                    .streamClient()
                    .newStreamPrototype()
                    .setOnResponseHeaders((responseHeaders, lastCallback) -> {
                      onResponseHeaders(responseHeaders, lastCallback);
                      return null;
                    })
                    .setOnResponseData((data, lastCallback) -> {
                      mEnvoyCallbackExecutor.pause();
                      mEndStream = lastCallback;
                      if (!mMostRecentBufferRead.compareAndSet(null, data)) {
                        throw new IllegalStateException("mostRecentBufferRead should be clear.");
                      }
                      processReadResult();
                      return null;
                    })
                    .setOnError(error -> {
                      String message = "failed with error after " + error.getAttemptCount() +
                                       " attempts. Message=[" + error.getMessage() + "] Code=[" +
                                       error.getErrorCode() + "]";
                      Throwable throwable = new CronetExceptionImpl(message, error.getCause());
                      mCronvoyExecutor.execute(() -> enterCronetErrorState(throwable));
                      return null;
                    })
                    .setOnCancel(() -> {
                      mCancelCalled.set(true);
                      cancel();
                      return null;
                    })
                    .start(mEnvoyCallbackExecutor)
                    .sendHeaders(envoyRequestHeaders, mUploadDataProvider == null);
      if (mUploadDataProvider != null) {
        mOutputStreamDataSink = new OutputStreamDataSink();
        // If this is not the first time, then UploadDataProvider.rewind() will be invoked first.
        mOutputStreamDataSink.start(/* firstTime= */ mUrlChain.size() == 1);
      }
    }));
  }

  private static RequestHeaders buildEnvoyRequestHeaders(URL url, boolean isHttp2Enabled,
                                                         String initialMethod,
                                                         Map<String, String> requestHeaders,
                                                         String mUserAgent) {
    RequestMethod requestMethod = RequestMethod.valueOf(initialMethod);
    RequestHeadersBuilder requestHeadersBuilder = new RequestHeadersBuilder(
        requestMethod, url.getProtocol(), url.getAuthority(), url.getPath());
    if (!requestHeaders.containsKey(USER_AGENT)) {
      requestHeaders.put(USER_AGENT, mUserAgent);
    }
    for (Map.Entry<String, String> entry : requestHeaders.entrySet()) {
      if (entry.getValue() == null) {
        continue;
      }
      requestHeadersBuilder.add(entry.getKey(), entry.getValue());
    }
    UpstreamHttpProtocol protocol = isHttp2Enabled && url.getProtocol().equalsIgnoreCase("https")
                                        ? UpstreamHttpProtocol.HTTP2
                                        : UpstreamHttpProtocol.HTTP1;
    RequestHeaders envoyRequestHeaders =
        requestHeadersBuilder.addUpstreamHttpProtocol(protocol).build();
    return envoyRequestHeaders;
  }

  private Runnable errorSetting(final CheckedRunnable delegate) {
    return () -> {
      try {
        delegate.run();
      } catch (Throwable t) {
        enterCronetErrorState(t);
      }
    };
  }

  private Runnable userErrorSetting(final CheckedRunnable delegate) {
    return () -> {
      try {
        delegate.run();
      } catch (Throwable t) {
        enterUserErrorState(t);
      }
    };
  }

  private Runnable uploadErrorSetting(final CheckedRunnable delegate) {
    return () -> {
      try {
        delegate.run();
      } catch (Throwable t) {
        enterUploadErrorState(t);
      }
    };
  }

  @Override
  public void read(final ByteBuffer buffer) {
    Preconditions.checkDirect(buffer);
    Preconditions.checkHasRemaining(buffer);
    transitionStates(State.AWAITING_READ, State.READING,
                     () -> mCronvoyExecutor.execute(errorSetting(() -> {
                       if (!mUserCurrentReadBuffer.compareAndSet(null, buffer)) {
                         throw new IllegalStateException("userCurrentReadBuffer should be clear");
                       }
                       processReadResult();
                     })));
  }

  private void processReadResult() {
    ByteBuffer sourceBuffer = mMostRecentBufferRead.get();
    if (sourceBuffer == null) {
      return;
    }
    ByteBuffer sinkBuffer = mUserCurrentReadBuffer.getAndSet(null);
    if (sinkBuffer == null) {
      return;
    }
    while (sinkBuffer.hasRemaining() && sourceBuffer.hasRemaining()) {
      sinkBuffer.put(sourceBuffer.get());
    }
    if (sourceBuffer.hasRemaining() || !mEndStream) {
      mCallbackAsync.onReadCompleted(mUrlResponseInfo, sinkBuffer);
      return;
    }
    if (mState.compareAndSet(/* expect= */ State.READING,
                             /* update= */ State.COMPLETE)) {
      mCallbackAsync.onSucceeded(mUrlResponseInfo);
    }
  }

  @Override
  public void cancel() {
    @State int oldState = mState.getAndSet(State.CANCELLED);
    switch (oldState) {
    case State.REDIRECT_RECEIVED:
    case State.AWAITING_FOLLOW_REDIRECT:
    case State.AWAITING_READ:
    case State.STARTED:
    case State.READING:
      fireCloseUploadDataProvider();
      if (mStream != null && mCancelCalled.compareAndSet(false, true)) {
        mStream.cancel();
      }
      mCallbackAsync.onCanceled(mUrlResponseInfo);
      break;
    // The rest are all termination cases - we're too late to cancel.
    case State.ERROR:
    case State.COMPLETE:
    case State.CANCELLED:
      break;
    default:
      break;
    }
  }

  @Override
  public boolean isDone() {
    @State int state = mState.get();
    return state == State.COMPLETE || state == State.ERROR || state == State.CANCELLED;
  }

  @Override
  public void getStatus(StatusListener listener) {
    @State int state = mState.get();
    int extraStatus = mAdditionalStatusDetails;

    @StatusValues final int status;
    switch (state) {
    case State.ERROR:
    case State.COMPLETE:
    case State.CANCELLED:
    case State.NOT_STARTED:
      status = Status.INVALID;
      break;
    case State.STARTED:
      status = extraStatus;
      break;
    case State.REDIRECT_RECEIVED:
    case State.AWAITING_FOLLOW_REDIRECT:
    case State.AWAITING_READ:
      status = Status.IDLE;
      break;
    case State.READING:
      status = Status.READING_RESPONSE;
      break;
    default:
      throw new IllegalStateException("Switch is exhaustive: " + state);
    }

    mCallbackAsync.sendStatus(new VersionSafeCallbacks.UrlRequestStatusListener(listener), status);
  }

  /** This wrapper ensures that callbacks are always called on the correct executor */
  private class AsyncUrlRequestCallback {
    final VersionSafeCallbacks.UrlRequestCallback mCallback;
    final Executor mUserExecutor;
    final Executor mFallbackExecutor;

    AsyncUrlRequestCallback(Callback callback, final Executor userExecutor) {
      mCallback = new VersionSafeCallbacks.UrlRequestCallback(callback);
      if (mAllowDirectExecutor) {
        mUserExecutor = userExecutor;
        mFallbackExecutor = null;
      } else {
        mUserExecutor = new DirectPreventingExecutor(userExecutor);
        mFallbackExecutor = userExecutor;
      }
    }

    void sendStatus(final VersionSafeCallbacks.UrlRequestStatusListener listener,
                    final int status) {
      mUserExecutor.execute(() -> listener.onStatus(status));
    }

    void execute(CheckedRunnable runnable) {
      try {
        mUserExecutor.execute(userErrorSetting(runnable));
      } catch (RejectedExecutionException e) {
        enterErrorState(new CronetExceptionImpl("Exception posting task to executor", e));
      }
    }

    void onRedirectReceived(final UrlResponseInfo info, final String newLocationUrl) {
      execute(() -> mCallback.onRedirectReceived(CronetUrlRequest.this, info, newLocationUrl));
    }

    void onResponseStarted(UrlResponseInfo info) {
      execute(() -> {
        if (mState.compareAndSet(
                /* expect= */ State.STARTED,
                /* update= */ State.AWAITING_READ)) {
          mCallback.onResponseStarted(CronetUrlRequest.this, info);
        }
      });
    }

    void onReadCompleted(final UrlResponseInfo info, final ByteBuffer byteBuffer) {
      execute(() -> {
        if (mState.compareAndSet(
                /* expect= */ State.READING,
                /* update= */ State.AWAITING_READ)) {
          boolean envoyCallbackExecutorCanResume = !mMostRecentBufferRead.get().hasRemaining();
          if (envoyCallbackExecutorCanResume) {
            mMostRecentBufferRead.set(null);
          }
          mCallback.onReadCompleted(CronetUrlRequest.this, info, byteBuffer);
          if (envoyCallbackExecutorCanResume) {
            mEnvoyCallbackExecutor.resume();
          }
        }
      });
    }

    void onCanceled(final UrlResponseInfo info) {
      mUserExecutor.execute(() -> {
        try {
          mCallback.onCanceled(CronetUrlRequest.this, info);
        } catch (Exception exception) {
          Log.e(TAG, "Exception in onCanceled method", exception);
        }
      });
    }

    void onSucceeded(final UrlResponseInfo info) {
      mUserExecutor.execute(() -> {
        try {
          mCallback.onSucceeded(CronetUrlRequest.this, info);
        } catch (Exception exception) {
          Log.e(TAG, "Exception in onSucceeded method", exception);
        }
      });
    }

    void onFailed(final UrlResponseInfo urlResponseInfo, final CronetException e) {
      Runnable runnable = () -> {
        try {
          mCallback.onFailed(CronetUrlRequest.this, urlResponseInfo, e);
        } catch (Exception exception) {
          Log.e(TAG, "Exception in onFailed method", exception);
        }
      };
      try {
        mUserExecutor.execute(runnable);
      } catch (InlineExecutionProhibitedException wasDirect) {
        if (mFallbackExecutor != null) {
          mFallbackExecutor.execute(runnable);
        }
      }
    }
  }

  // Executor that runs one task at a time on an underlying Executor. It can be paused/resumed.
  // NOTE: Do not use to wrap user supplied Executor as lock is held while underlying execute()
  // is called.
  private static final class PausableSerializingExecutor implements Executor {

    private final Executor mUnderlyingExecutor;
    private final Runnable mRunTasks = new Runnable() {
      @Override
      public void run() {
        Runnable task;
        synchronized (mTaskQueue) {
          if (mRunning || mPaused) {
            return;
          }
          task = mTaskQueue.pollFirst();
          mRunning = task != null;
        }
        while (task != null) {
          boolean threw = true;
          try {
            task.run();
            threw = false;
          } finally {
            synchronized (mTaskQueue) {
              if (threw) {
                // If task.run() threw, this method will abort without looping
                // again, so repost to keep running tasks.
                mRunning = false;
                try {
                  mUnderlyingExecutor.execute(mRunTasks);
                } catch (RejectedExecutionException e) {
                  // Give up if a task run at shutdown throws.
                }
              } else if (mPaused) {
                task = null;
                mRunning = false;
              } else {
                task = mTaskQueue.pollFirst();
                mRunning = task != null;
              }
            }
          }
        }
      }
    };
    // Queue of tasks to run. Tasks are added to the end and taken from the front.
    // Synchronized on itself.
    @GuardedBy("mTaskQueue") private final Deque<Runnable> mTaskQueue = new ArrayDeque<>();
    // Indicates if runTasks is actively running tasks.
    @GuardedBy("mTaskQueue") private boolean mRunning;
    // Indicates if runTasks is temporarily disabled. Still, tasks keep accumulating as usual.
    @GuardedBy("mTaskQueue") private boolean mPaused;
    // Indicates if this executor can still queue/execute tasks. If shutdown is true, it can't.
    @GuardedBy("mTaskQueue") private boolean mShutdown;

    PausableSerializingExecutor(Executor underlyingExecutor) {
      mUnderlyingExecutor = underlyingExecutor;
    }

    void pause() {
      synchronized (mTaskQueue) { mPaused = true; }
    }

    void resume() {
      synchronized (mTaskQueue) {
        mPaused = false;
        if (!mTaskQueue.isEmpty()) {
          try {
            mUnderlyingExecutor.execute(mRunTasks);
          } catch (RejectedExecutionException e) {
            // Ignoring is fine here - this is shutting down.
          }
        }
      }
    }

    void shutdown() {
      synchronized (mTaskQueue) {
        mShutdown = true;
        mTaskQueue.clear();
      }
    }

    @Override
    public void execute(Runnable command) {
      synchronized (mTaskQueue) {
        if (mShutdown) {
          return;
        }
        mTaskQueue.addLast(command);
        if (!mPaused && !mTaskQueue.isEmpty()) {
          try {
            mUnderlyingExecutor.execute(mRunTasks);
          } catch (RejectedExecutionException e) {
            // Shutting down, do not add new task to the queue.
            mTaskQueue.removeLast();
          }
        }
      }
    }
  }
}
