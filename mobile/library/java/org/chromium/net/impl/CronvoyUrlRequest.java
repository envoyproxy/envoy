package org.chromium.net.impl;

import static org.chromium.net.impl.Errors.isQuicException;
import static org.chromium.net.impl.Errors.mapEnvoyMobileErrorToNetError;
import static org.chromium.net.impl.Errors.mapNetErrorToCronetApiErrorCode;

import android.os.ConditionVariable;
import android.util.Log;
import androidx.annotation.IntDef;
import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream;
import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.net.MalformedURLException;
import java.net.URI;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.AbstractMap;
import java.util.AbstractMap.SimpleEntry;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.chromium.net.CallbackException;
import org.chromium.net.CronetException;
import org.chromium.net.InlineExecutionProhibitedException;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.RequestFinishedInfo.Metrics;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.impl.Errors.NetError;

/** UrlRequest, backed by Envoy-Mobile. */
public final class CronvoyUrlRequest extends CronvoyUrlRequestBase {

  /**
   * State interface for keeping track of the internal state of a {@link UrlRequestBase}.
   * <pre>
   *               /- AWAITING_FOLLOW_REDIRECT <-\     /- READING <--\
   *               |                             |     |             |
   *               V                             /     V             /
   * NOT_STARTED -> STARTED --------------------------> AWAITING_READ --------> COMPLETE
   * </pre>
   */
  @IntDef({State.NOT_STARTED, State.STARTED, State.AWAITING_FOLLOW_REDIRECT, State.AWAITING_READ,
           State.READING, State.ERROR, State.COMPLETE, State.CANCELLED, State.PENDING_CANCEL,
           State.ERROR_PENDING_CANCEL})
  @Retention(RetentionPolicy.SOURCE)
  @interface State {
    int NOT_STARTED = 0;
    int STARTED = 1;
    int AWAITING_FOLLOW_REDIRECT = 2;
    int AWAITING_READ = 3;
    int READING = 4;
    int ERROR = 5;
    int COMPLETE = 6;
    int CANCELLED = 7;
    int PENDING_CANCEL = 8;
    int ERROR_PENDING_CANCEL = 9;
  }

  @IntDef({CancelState.READY, CancelState.BUSY, CancelState.CANCELLED})
  @Retention(RetentionPolicy.SOURCE)
  private @interface CancelState {
    int READY = 0;
    int BUSY = 1;
    int CANCELLED = 2;
  }

  @IntDef(flag = true, // This is a bitmap.
          value = {SucceededState.UNDETERMINED, SucceededState.FINAL_READ_DONE,
                   SucceededState.ON_COMPLETE_RECEIVED, SucceededState.SUCCESS_READY})
  @Retention(RetentionPolicy.SOURCE)
  private @interface SucceededState {
    int UNDETERMINED = 0b00;
    int FINAL_READ_DONE = 0b01;
    int ON_COMPLETE_RECEIVED = 0b10;
    int SUCCESS_READY = FINAL_READ_DONE | ON_COMPLETE_RECEIVED;
  }

  @IntDef(flag = true, // This is a bitmap.
          value = {ReportState.INITIAL_STATE, ReportState.USER_FINAL_CALLBACK_DONE,
                   ReportState.NETWORK_FINAL_CALLBACK_RECEIVED, ReportState.REPORT_READY})
  @Retention(RetentionPolicy.SOURCE)
  private @interface ReportState {
    int INITIAL_STATE = 0b00;
    int USER_FINAL_CALLBACK_DONE = 0b01;
    int NETWORK_FINAL_CALLBACK_RECEIVED = 0b10;
    int REPORT_READY = USER_FINAL_CALLBACK_DONE | NETWORK_FINAL_CALLBACK_RECEIVED;
  }

  private static final String X_ENVOY = "x-envoy";
  private static final String X_ENVOY_UPSTREAM_ALPN = "x-envoy-upstream-alpn";
  private static final String TAG = CronvoyUrlRequest.class.getSimpleName();
  private static final String USER_AGENT = "User-Agent";
  private static final String CONTENT_TYPE = "Content-Type";
  private final String mUserAgent;
  private final HeadersList mRequestHeaders = new HeadersList();
  private final Collection<Object> mRequestAnnotations;
  private final CronvoyUrlRequestContext mRequestContext;
  private final AtomicBoolean mWaitingOnRedirect = new AtomicBoolean(false);
  private final AtomicBoolean mWaitingOnRead = new AtomicBoolean(false);
  private volatile ByteBuffer mUserCurrentReadBuffer = null;

  /**
   * This is the source of thread safety in this class - no other synchronization is performed. By
   * compare-and-swapping from one state to another, we guarantee that operations aren't running
   * concurrently. Only the winner of a compare-and-swapping proceeds.
   *
   * <p>A caller can lose a compare-and-swapping for three reasons - user error (two calls to read()
   * without waiting for the read to succeed), runtime error (network code or user code throws an
   * exception), or cancellation.
   */
  private final AtomicInteger mState = new AtomicInteger(State.NOT_STARTED);
  /**
   * Ensures that the "mCallback.onSucceeded" callback will be invoked after observing the required
   * events, and that it will be done only once.
   *
   * <p>At the end of a successful request, "mCallback.onSucceeded" is invoked. Before doing so,
   * two events must have occurred first: the "completion of the final read" and the "onComplete
   * Network callback". The Thread involved with the last of these two events is in charge of the
   * registering the task to execute "mCallback.onSucceeded" - this is intrinsically racy.
   */
  private final AtomicCombinatoryState mSucceededState =
      new AtomicCombinatoryState(SucceededState.SUCCESS_READY);
  /**
   * Ensures that the CronvoyMetrics will be posted after observing the required events, and that it
   * will be done only once.
   *
   * <p>At the end of a request, mRequestFinishedListener is used to post the CronvoyMetrics. Before
   * doing so, two events must have occurred first: the "final user callback" and the "final Network
   * callback". The Thread involved with the last of these two events is in charge of the posting -
   * this is intrinsically racy.
   */
  private final AtomicCombinatoryState mReportState =
      new AtomicCombinatoryState(ReportState.REPORT_READY);

  private final boolean mAllowDirectExecutor;

  /* These don't change with redirects */
  private String mInitialMethod;
  private final Executor mUserExecutor;
  private final CronvoyVersionSafeCallbacks.UrlRequestCallback mCallback;
  private final String mInitialUrl;
  private final CronvoyVersionSafeCallbacks.RequestFinishedInfoListener mRequestFinishedListener;
  private final ConditionVariable mStartBlock = new ConditionVariable();

  private CronvoyUploadDataStream mUploadDataStream;

  private volatile CronetException mException;

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
  private final AtomicReference<EnvoyHTTPStream> mStream = new AtomicReference<>();
  private final List<String> mUrlChain = new ArrayList<>();
  private volatile EnvoyFinalStreamIntel mEnvoyFinalStreamIntel;
  private long mBytesReceivedFromRedirects = 0;
  private long mBytesReceivedFromLastRedirect = 0;
  private CronvoyHttpCallbacks mCronvoyCallbacks;
  private String mCurrentUrl;
  private volatile CronvoyUrlResponseInfoImpl mUrlResponseInfo;
  private String mPendingRedirectUrl;

  /**
   * @param executor The executor for orchestrating tasks between envoy-mobile callbacks
   */
  CronvoyUrlRequest(CronvoyUrlRequestContext cronvoyEngine, String url, Callback callback,
                    Executor executor, String userAgent, boolean allowDirectExecutor,
                    Collection<Object> connectionAnnotations, boolean trafficStatsTagSet,
                    int trafficStatsTag, boolean trafficStatsUidSet, int trafficStatsUid,
                    RequestFinishedInfo.Listener requestFinishedListener) {
    if (url == null) {
      throw new NullPointerException("URL is required");
    }
    if (callback == null) {
      throw new NullPointerException("Listener is required");
    }
    if (executor == null) {
      throw new NullPointerException("Executor is required");
    }
    mCallback = new CronvoyVersionSafeCallbacks.UrlRequestCallback(callback);
    mRequestFinishedListener =
        requestFinishedListener != null
            ? new CronvoyVersionSafeCallbacks.RequestFinishedInfoListener(requestFinishedListener)
            : null;
    mRequestContext = cronvoyEngine;
    mAllowDirectExecutor = allowDirectExecutor;
    mUserExecutor = executor;
    mInitialUrl = url;
    mCurrentUrl = url;
    mUserAgent = userAgent;
    mRequestAnnotations = connectionAnnotations;
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

  @Override
  public void addHeader(String header, String value) {
    checkNotStarted();
    if (header == null) {
      throw new NullPointerException("Invalid header name.");
    }
    if (value == null) {
      throw new NullPointerException("Invalid header value.");
    }
    if (!isValidHeaderName(header) || value.contains("\r\n")) {
      throw new IllegalArgumentException("Invalid header " + header + "=" + value);
    }
    mRequestHeaders.add(new AbstractMap.SimpleImmutableEntry<>(header, value));
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
    if (mInitialMethod == null) {
      mInitialMethod = "POST";
    }
    mUploadDataStream = new CronvoyUploadDataStream(uploadDataProvider, executor, this);
  }

  @Override
  public void start() {
    if (mState.compareAndSet(State.NOT_STARTED, State.STARTED)) {
      mRequestContext.setTaskToExecuteWhenInitializationIsCompleted(mStartBlock::open);
      mStartBlock.block();
      fireOpenConnection();
    } else {
      throw new IllegalStateException("Request is already started.");
    }
  }

  @Override
  public void read(final ByteBuffer buffer) {
    CronvoyPreconditions.checkDirect(buffer);
    CronvoyPreconditions.checkHasRemaining(buffer);
    if (!mWaitingOnRead.compareAndSet(true, false)) {
      throw new IllegalStateException("Unexpected read attempt.");
    }
    if (mState.compareAndSet(State.AWAITING_READ, streamEnded() ? State.COMPLETE : State.READING)) {
      if (streamEnded()) {
        if (mSucceededState.hasReachedFinalState(SucceededState.FINAL_READ_DONE)) {
          onSucceeded();
        }
        return;
      }
      mUserCurrentReadBuffer = buffer;
      mCronvoyCallbacks.readData(buffer.remaining());
    }
    // When mWaitingOnRead is true (did not throw), it means that we were duly waiting
    // for the User to invoke this method. If the mState.compareAndSet() failed, it means
    // that this was cancelled, or somehow onError() was called. For both cases, either a "cancel"
    // was induced to get a callback, or the user already had the onFailed() or onCancelled()
    // invoked. The original Cronet logic in this case is to do nothing.
  }

  @Override
  public void followRedirect() {
    if (!mWaitingOnRedirect.compareAndSet(true, false)) {
      throw new IllegalStateException("No redirect to follow.");
    }
    mCurrentUrl = mPendingRedirectUrl;
    mPendingRedirectUrl = null;
    if (mUploadDataStream != null) {
      mUploadDataStream.rewind();
    } else {
      if (mState.compareAndSet(State.AWAITING_FOLLOW_REDIRECT, State.STARTED)) {
        fireOpenConnection();
      }
      // When mWaitingOnRedirect is true (did not throw), it means that we were duly waiting
      // for the User to invoke this method. If the mState.compareAndSet() failed, it means
      // that this was cancelled, or somehow onError() was called. For both cases, the user already
      // had the onFailed() or onCancelled() invoked. mState can not be PENDING_CANCEL or
      // ERROR_PENDING_CANCEL, because at this point there is no Engine running.
    }
  }

  void followRedirectAfterSuccessfulRewind() {
    if (mState.compareAndSet(State.AWAITING_FOLLOW_REDIRECT, State.STARTED)) {
      fireOpenConnection();
    }
  }

  @Override
  public boolean isDone() {
    @State int state = mState.get();
    return state == State.COMPLETE || state == State.ERROR || state == State.CANCELLED;
  }

  @Override
  public void getStatus(StatusListener listener) {
    @StatusValues int extraStatus = mAdditionalStatusDetails;
    @State int state = mState.get();

    @StatusValues final int status;
    switch (state) {
    case State.ERROR:
    case State.COMPLETE:
    case State.CANCELLED:
    case State.PENDING_CANCEL:
    case State.ERROR_PENDING_CANCEL:
    case State.NOT_STARTED:
      status = Status.INVALID;
      break;
    case State.STARTED:
      status = extraStatus;
      break;
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

    sendStatus(new CronvoyVersionSafeCallbacks.UrlRequestStatusListener(listener), status);
  }

  @State
  private static int determineNextCancelState(boolean streamEnded, @State int originalState) {
    switch (originalState) {
    case State.STARTED:
    case State.AWAITING_READ:
    case State.READING:
      return streamEnded ? State.CANCELLED : State.PENDING_CANCEL;
    case State.AWAITING_FOLLOW_REDIRECT:
      return State.CANCELLED;
    case State.PENDING_CANCEL:
    case State.ERROR_PENDING_CANCEL:
    case State.NOT_STARTED: // Invoking cancel when NOT_STARTED has no effect.
    case State.ERROR:
    case State.COMPLETE:
    case State.CANCELLED:
      return originalState;
    default:
      throw new IllegalStateException("Switch is exhaustive: " + originalState);
    }
  }

  @Override
  public void cancel() {
    @State int originalState;
    @State int updatedState;
    do {
      originalState = mState.get();
      updatedState = determineNextCancelState(streamEnded(), originalState);
    } while (!mState.compareAndSet(originalState, updatedState));
    if (isTerminalState(originalState) || originalState == State.NOT_STARTED) {
      return;
    }
    fireCloseUploadDataProvider();
    if (updatedState == State.PENDING_CANCEL) {
      CronvoyHttpCallbacks cronvoyCallbacks = this.mCronvoyCallbacks;
      if (cronvoyCallbacks != null) {
        cronvoyCallbacks.cancel();
      }
      return;
    }

    // There is no Engine running - no callback will invoke onFailed() - hence done here.
    onCanceled();
  }

  @State
  private static int determineNextErrorState(boolean streamEnded, @State int originalState) {
    switch (originalState) {
    case State.STARTED:
    case State.AWAITING_READ:
    case State.READING:
      return streamEnded ? State.ERROR : State.ERROR_PENDING_CANCEL;
    case State.AWAITING_FOLLOW_REDIRECT:
      return State.ERROR;
    case State.PENDING_CANCEL:
    case State.ERROR_PENDING_CANCEL:
    case State.NOT_STARTED: // This is invalid and will be caught later.
    case State.ERROR:
    case State.COMPLETE:
    case State.CANCELLED:
    default:
      return originalState;
    }
  }

  // No-op if already in a terminal state.
  private void enterErrorState(CronetException error) {
    @State int originalState;
    @State int updatedState;
    do {
      originalState = mState.get();
      updatedState = determineNextErrorState(streamEnded(), originalState);
    } while (!mState.compareAndSet(originalState, updatedState));
    if (originalState == State.NOT_STARTED) {
      throw new IllegalStateException("Can't enter error state before start");
    }
    if (isTerminalState(originalState)) {
      return;
    }
    mException = error;
    fireCloseUploadDataProvider();
    if (updatedState == State.ERROR_PENDING_CANCEL) {
      CronvoyHttpCallbacks cronvoyCallbacks = this.mCronvoyCallbacks;
      if (cronvoyCallbacks != null) {
        cronvoyCallbacks.cancel();
      }
      return;
    }

    // There is no Engine running - no callback will invoke onFailed() - hence done here.
    onFailed();
  }

  private static boolean isTerminalState(@State int state) {
    switch (state) {
    case State.ERROR:
    case State.COMPLETE:
    case State.CANCELLED:
    case State.PENDING_CANCEL:
    case State.ERROR_PENDING_CANCEL:
      return true;
    default:
      return false;
    }
  }

  private void enterCronetErrorState(final Throwable error) {
    enterErrorState(new CronvoyExceptionImpl("System error", error));
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
      if (!isTerminalState(state)) {
        throw new IllegalStateException("Invalid state transition - expected " + expected +
                                        " but was " + state);
      }
    } else {
      afterTransition.run();
    }
  }

  private void fireCloseUploadDataProvider() {
    if (mUploadDataStream != null) {
      mUploadDataStream.close(); // Idempotent
    }
  }

  // This method is only called when in STARTED state. This means a "cancel" request won't be
  // executed immediately - that quite important here, otherwise this would lead to unfortunate
  // race conditions. A "cancel" request will then be honnored on the first callback.
  private void fireOpenConnection() {
    if (mInitialMethod == null) {
      mInitialMethod = "GET";
    }
    mUrlResponseInfo = null;
    mEnvoyFinalStreamIntel = null;
    mBytesReceivedFromRedirects += mBytesReceivedFromLastRedirect;
    mAdditionalStatusDetails = Status.CONNECTING;
    mUrlChain.add(mCurrentUrl);
    Map<String, List<String>> envoyRequestHeaders = buildEnvoyRequestHeaders(
        mInitialMethod, mRequestHeaders, mUploadDataStream, mUserAgent, mCurrentUrl);
    mCronvoyCallbacks = new CronvoyHttpCallbacks();
    mStream.set(mRequestContext.getEnvoyEngine().startStream(mCronvoyCallbacks,
                                                             /* explicitFlowControl= */ true));
    mStream.get().sendHeaders(envoyRequestHeaders, mUploadDataStream == null);
    if (mUploadDataStream != null && mUrlChain.size() == 1) {
      mUploadDataStream.initializeWithRequest();
    }
  }

  private static Map<String, List<String>>
  buildEnvoyRequestHeaders(String initialMethod, HeadersList headersList,
                           CronvoyUploadDataStream mUploadDataStream, String userAgent,
                           String currentUrl) {
    Map<String, List<String>> headers = new LinkedHashMap<>();
    final URL url;
    try {
      url = new URL(currentUrl);
    } catch (MalformedURLException e) {
      throw new IllegalArgumentException("Invalid URL", e);
    }
    headers.computeIfAbsent(":authority", unused -> new ArrayList<>()).add(url.getAuthority());
    headers.computeIfAbsent(":method", unused -> new ArrayList<>()).add(initialMethod);
    headers.computeIfAbsent(":path", unused -> new ArrayList<>()).add(url.getFile());
    headers.computeIfAbsent(":scheme", unused -> new ArrayList<>()).add(url.getProtocol());
    boolean hasUserAgent = false;
    boolean hasContentType = false;
    for (Map.Entry<String, String> header : headersList) {
      if (header.getKey().isEmpty()) {
        throw new IllegalArgumentException("Invalid header =");
      }
      hasUserAgent = hasUserAgent ||
                     (header.getKey().equalsIgnoreCase(USER_AGENT) && !header.getValue().isEmpty());
      hasContentType = hasContentType || (header.getKey().equalsIgnoreCase(CONTENT_TYPE) &&
                                          !header.getValue().isEmpty());
      headers.computeIfAbsent(header.getKey(), unused -> new ArrayList<>()).add(header.getValue());
    }
    if (!hasUserAgent) {
      headers.computeIfAbsent(USER_AGENT, unused -> new ArrayList<>()).add(userAgent);
    }
    if (!hasContentType && mUploadDataStream != null) {
      throw new IllegalArgumentException("Requests with upload data must have a Content-Type.");
    }

    return headers;
  }

  /**
   * If callback method throws an exception, request gets canceled
   * and exception is reported via onFailed listener callback.
   * Only called on the Executor.
   */
  private void onCallbackException(Throwable t) {
    CallbackException requestError =
        new CronvoyCallbackExceptionImpl("Exception received from UrlRequest.Callback", t);
    Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in CalledByNative method", t);
    enterErrorState(requestError);
  }

  /**
   * Called when UploadDataProvider encounters an error.
   */
  void onUploadException(Exception t) {
    CallbackException uploadError =
        new CronvoyCallbackExceptionImpl("Exception received from UploadDataProvider", t);
    Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in upload method", t);
    enterErrorState(uploadError);
  }

  /** This wrapper ensures that callbacks are always called on the correct executor */
  void sendStatus(final CronvoyVersionSafeCallbacks.UrlRequestStatusListener listener,
                  final int status) {
    mUserExecutor.execute(() -> listener.onStatus(status));
  }

  void execute(Runnable runnable) {
    try {
      mUserExecutor.execute(runnable);
    } catch (RejectedExecutionException e) {
      enterErrorState(new CronvoyExceptionImpl("Exception posting task to executor", e));
    }
  }

  void onCanceled() {
    Runnable task = new Runnable() {
      @Override
      public void run() {
        try {
          mCallback.onCanceled(CronvoyUrlRequest.this, mUrlResponseInfo);
          maybeReportMetrics(ReportState.USER_FINAL_CALLBACK_DONE);
        } catch (Exception exception) {
          Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in onCanceled method", exception);
        }
      }
    };
    execute(task);
  }

  void onSucceeded() {
    Runnable task = new Runnable() {
      @Override
      public void run() {
        try {
          mCallback.onSucceeded(CronvoyUrlRequest.this, mUrlResponseInfo);
          maybeReportMetrics(ReportState.USER_FINAL_CALLBACK_DONE);
        } catch (Exception exception) {
          Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in onSucceeded method", exception);
        }
      }
    };
    execute(task);
  }

  void onFailed() {
    Runnable task = new Runnable() {
      @Override
      public void run() {
        try {
          mCallback.onFailed(CronvoyUrlRequest.this, mUrlResponseInfo, mException);
          maybeReportMetrics(ReportState.USER_FINAL_CALLBACK_DONE);
        } catch (Exception exception) {
          Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception in onFailed method", exception);
        }
      }
    };
    execute(task);
  }

  void send(ByteBuffer buffer, boolean finalChunk) {
    CronvoyHttpCallbacks cronvoyCallbacks = this.mCronvoyCallbacks;
    if (cronvoyCallbacks != null) {
      cronvoyCallbacks.send(buffer, finalChunk);
    }
  }

  boolean isAllowDirectExecutor() { return mAllowDirectExecutor; }

  /** Enforces prohibition of direct execution. */
  void checkCallingThread() {
    if (!mAllowDirectExecutor && mRequestContext.isNetworkThread(Thread.currentThread())) {
      throw new InlineExecutionProhibitedException();
    }
  }

  private void checkNotStarted() {
    @State int state = mState.get();
    if (state != State.NOT_STARTED) {
      throw new IllegalStateException("Request is already started. State is: " + state);
    }
  }

  private boolean streamEnded() {
    CronvoyHttpCallbacks cronvoyCallbacks = this.mCronvoyCallbacks;
    return cronvoyCallbacks != null && cronvoyCallbacks.mEndStream;
  }

  private void recordEnvoyFinalStreamIntel(EnvoyFinalStreamIntel envoyFinalStreamIntel) {
    mEnvoyFinalStreamIntel = envoyFinalStreamIntel;
    if (mUrlResponseInfo != null) { // Null if cancelled before receiving a Response.
      mUrlResponseInfo.setReceivedByteCount(envoyFinalStreamIntel.getReceivedByteCount() +
                                            mBytesReceivedFromRedirects);
    }
    maybeReportMetrics(ReportState.NETWORK_FINAL_CALLBACK_RECEIVED);
  }

  private void recordEnvoyStreamIntel(EnvoyStreamIntel envoyStreamIntel) {
    mUrlResponseInfo.setReceivedByteCount(envoyStreamIntel.getConsumedBytesFromResponse() +
                                          mBytesReceivedFromRedirects);
  }

  private void maybeReportMetrics(@ReportState int reportStateEvent) {
    if (!mReportState.hasReachedFinalState(reportStateEvent)) {
      return;
    }
    Metrics metrics = getMetrics(mEnvoyFinalStreamIntel, mBytesReceivedFromRedirects);
    final RequestFinishedInfo requestInfo =
        new CronvoyRequestFinishedInfoImpl(mInitialUrl, mRequestAnnotations, metrics,
                                           getFinishedReason(), mUrlResponseInfo, mException);
    mRequestContext.reportRequestFinished(requestInfo);
    if (mRequestFinishedListener != null) {
      try {
        mRequestFinishedListener.getExecutor().execute(new Runnable() {
          @Override
          public void run() {
            mRequestFinishedListener.onRequestFinished(requestInfo);
          }
        });
      } catch (RejectedExecutionException failException) {
        Log.e(CronvoyUrlRequestContext.LOG_TAG, "Exception posting task to executor",
              failException);
      }
    }
  }

  private static Metrics getMetrics(EnvoyFinalStreamIntel intel, long bytesReceivedFromRedirects) {
    return new CronvoyMetrics(
        intel.getStreamStartMs(), intel.getDnsStartMs(), intel.getDnsEndMs(),
        intel.getConnectStartMs(), intel.getConnectEndMs(), intel.getSslStartMs(),
        intel.getSslEndMs(), intel.getSendingStartMs(), intel.getSendingEndMs(),
        /* pushStartMs= */ -1, /* pushEndMs= */ -1, intel.getResponseStartMs(),
        intel.getStreamEndMs(), intel.getSocketReused(), intel.getSentByteCount(),
        intel.getReceivedByteCount() + bytesReceivedFromRedirects);
  }

  @CronvoyRequestFinishedInfoImpl.FinishedReason
  private int getFinishedReason() {
    switch (mState.get()) {
    case State.COMPLETE:
      return CronvoyRequestFinishedInfoImpl.SUCCEEDED;
    case State.CANCELLED:
      return CronvoyRequestFinishedInfoImpl.CANCELED;
    default:
      return CronvoyRequestFinishedInfoImpl.FAILED;
    }
  }

  private static int determineNextState(boolean endStream, @State int original,
                                        @State int desired) {
    switch (original) {
    case State.PENDING_CANCEL:
      return endStream ? State.CANCELLED : State.PENDING_CANCEL;
    case State.ERROR_PENDING_CANCEL:
      return endStream ? State.ERROR : State.ERROR_PENDING_CANCEL;
    default:
      return desired;
    }
  }

  private static class HeadersList extends ArrayList<Map.Entry<String, String>> {}

  private class CronvoyHttpCallbacks implements EnvoyHTTPCallbacks {

    private final AtomicInteger mCancelState = new AtomicInteger(CancelState.READY);
    private volatile boolean mEndStream = false; // Accessed by different Threads

    @Override
    public void onHeaders(Map<String, List<String>> headers, boolean endStream,
                          EnvoyStreamIntel streamIntel) {
      mUrlResponseInfo = new CronvoyUrlResponseInfoImpl();
      recordEnvoyStreamIntel(streamIntel);
      mEndStream = endStream;
      List<String> statuses = headers.get(":status");
      final int responseCode =
          statuses != null && !statuses.isEmpty() ? Integer.valueOf(statuses.get(0)) : -1;
      final String locationField;
      if (responseCode >= 300 && responseCode < 400) {
        setUrlResponseInfo(headers, responseCode);
        List<String> locationFields = mUrlResponseInfo.getAllHeaders().get("location");
        if (locationFields != null && !locationFields.isEmpty() &&
            !locationFields.get(0).isEmpty()) {
          locationField = locationFields.get(0);
        } else {
          locationField = null;
        }
      } else {
        locationField = null;
      }
      @State
      int desiredNextState =
          locationField == null ? State.AWAITING_READ : State.AWAITING_FOLLOW_REDIRECT;
      @State int originalState;
      @State int updatedState;
      do {
        originalState = mState.get();
        updatedState = determineNextState(endStream, originalState, desiredNextState);
      } while (!mState.compareAndSet(originalState, updatedState));
      if (completeAbandonIfAny(originalState, updatedState)) {
        return;
      }
      if (reportInternalStateTransitionErrorIfAny(originalState, State.STARTED)) {
        return;
      }

      if (locationField != null) {
        mBytesReceivedFromLastRedirect = streamIntel.getConsumedBytesFromResponse();
        cancel(); // Abort the the original request - we are being redirected.
      }

      Runnable task = new Runnable() {
        @Override
        public void run() {
          checkCallingThread();
          try {
            if (locationField != null) {
              // This CronvoyHttpCallbacks instance is already in an abandoned state at this point:
              // mState == State.AWAITING_FOLLOW_REDIRECT. But mState will change soon, so this line
              // puts the final nail in the coffin. isAbandoned() can only keep returning true.
              mCronvoyCallbacks = null;
              mStream.set(null);
              mPendingRedirectUrl = URI.create(mCurrentUrl).resolve(locationField).toString();
              mWaitingOnRedirect.set(true);
              mCallback.onRedirectReceived(CronvoyUrlRequest.this, mUrlResponseInfo,
                                           mPendingRedirectUrl);
            } else {
              if (responseCode < 300 || responseCode >= 400) {
                setUrlResponseInfo(headers, responseCode);
              }
              fireCloseUploadDataProvider(); // Idempotent
              mWaitingOnRead.set(true);
              mCallback.onResponseStarted(CronvoyUrlRequest.this, mUrlResponseInfo);
            }
          } catch (Throwable t) {
            onCallbackException(t);
          }
        }
      };
      execute(task);
    }

    @Override
    public void onData(ByteBuffer data, boolean endStream, EnvoyStreamIntel streamIntel) {
      if (isAbandoned()) {
        return;
      }
      recordEnvoyStreamIntel(streamIntel);
      mEndStream = endStream;
      @State int originalState;
      @State int updatedState;

      // When endStream with no data is read, there will be no more calls to read().
      boolean isFinalRead = endStream && !data.hasRemaining();
      do {
        originalState = mState.get();
        updatedState = determineNextState(endStream, originalState,
                                          isFinalRead ? State.COMPLETE : State.AWAITING_READ);
      } while (!mState.compareAndSet(originalState, updatedState));
      if (isFinalRead) {
        // onComplete still needs to be called - this always returns false.
        mSucceededState.hasReachedFinalState(SucceededState.FINAL_READ_DONE);
      }
      if (completeAbandonIfAny(originalState, updatedState)) {
        return;
      }
      if (reportInternalStateTransitionErrorIfAny(originalState, State.READING)) {
        return;
      }

      ByteBuffer userBuffer = mUserCurrentReadBuffer;
      mUserCurrentReadBuffer = null; // Avoid the reference to a potentially large buffer.
      int dataRead = data.remaining();
      // It is important to copy the `data` into the `userBuffer` outside the thread execution
      // because the `data` is backed by a direct `ByteBuffer` and it will be destroyed once
      // the `onData` completes.
      userBuffer.put(data); // NPE ==> BUG, BufferOverflowException ==> User not behaving.
      Runnable task = new Runnable() {
        @Override
        public void run() {
          checkCallingThread();
          try {

            if (dataRead > 0 || !endStream) {
              mWaitingOnRead.set(true);
              mCallback.onReadCompleted(CronvoyUrlRequest.this, mUrlResponseInfo, userBuffer);
            }
          } catch (Throwable t) {
            onCallbackException(t);
          }
        }
      };
      execute(task);
    }

    @Override
    public void onTrailers(Map<String, List<String>> trailers, EnvoyStreamIntel streamIntel) {
      if (isAbandoned()) {
        return;
      }
      mEndStream = true;
      @State int originalState;
      @State int updatedState;
      do {
        originalState = mState.get();
        updatedState = determineNextState(mEndStream, originalState, originalState);
      } while (!mState.compareAndSet(originalState, updatedState));
      if (completeAbandonIfAny(originalState, updatedState)) {
        return;
      }
      if (mState.compareAndSet(State.READING, State.COMPLETE)) {
        // onComplete still needs to be called - this always returns false.
        mSucceededState.hasReachedFinalState(SucceededState.FINAL_READ_DONE);
      }
    }

    @Override
    public void onError(int errorCode, String message, int attemptCount,
                        EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel) {
      if (isAbandoned()) {
        return;
      }
      recordEnvoyFinalStreamIntel(finalStreamIntel);
      mEndStream = true;
      @State int originalState;
      @State int updatedState;
      do {
        originalState = mState.get();
        updatedState = determineNextState(mEndStream, originalState, originalState);
      } while (!mState.compareAndSet(originalState, updatedState));
      if (completeAbandonIfAny(originalState, updatedState)) {
        return;
      }

      NetError netError = mapEnvoyMobileErrorToNetError(finalStreamIntel);
      int javaError = mapNetErrorToCronetApiErrorCode(netError);

      if (isQuicException(javaError)) {
        // `message` is populated from StreamInfo::responseCodeDetails(), so `message` is used to
        // populate the error details in the exception.
        enterErrorState(new CronvoyQuicExceptionImpl("Exception in CronvoyUrlRequest: " + netError,
                                                     javaError, netError.getErrorCode(),
                                                     Errors.QUIC_INTERNAL_ERROR, message));
        return;
      }

      // `message` is populated from StreamInfo::responseCodeDetails(), so `message` is used to
      // populate the error details in the exception.
      enterErrorState(new CronvoyNetworkExceptionImpl("Exception in CronvoyUrlRequest: " + netError,
                                                      javaError, netError.getErrorCode(), message));
    }

    @Override
    public void onCancel(EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel) {
      if (isAbandoned()) {
        return;
      }
      recordEnvoyFinalStreamIntel(finalStreamIntel);
      mEndStream = true;
      @State int originalState;
      @State int updatedState;
      do {
        originalState = mState.get();
        updatedState = determineNextState(mEndStream, originalState, originalState);
      } while (!mState.compareAndSet(originalState, updatedState));
      if (completeAbandonIfAny(originalState, updatedState)) {
        return;
      }

      CronetException exception = new CronvoyExceptionImpl("Cancelled", /* cause= */ null);
      enterErrorState(exception);
    }

    @Override
    public void onSendWindowAvailable(EnvoyStreamIntel streamIntel) {
      if (isAbandoned()) {
        return;
      }
      @State int originalState;
      @State int updatedState;
      do {
        originalState = mState.get();
        updatedState = determineNextState(mEndStream, originalState, originalState);
      } while (!mState.compareAndSet(originalState, updatedState));
      if (completeAbandonIfAny(originalState, updatedState)) {
        return;
      }
      if (reportInternalStateTransitionErrorIfAny(originalState, State.STARTED)) {
        return;
      }

      mUploadDataStream.readDataReady(); // Have the next request body chunk to be sent.
    }

    @Override
    public void onComplete(EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel) {
      if (isAbandoned()) {
        return;
      }
      recordEnvoyFinalStreamIntel(finalStreamIntel);
      if (mSucceededState.hasReachedFinalState(SucceededState.ON_COMPLETE_RECEIVED)) {
        onSucceeded();
      }
    }

    /**
     * Sends one chunk of the request body if the state permits. This method is not re-entrant, but
     * by contract this method can only be invoked once for the first chunk, and then once per
     * onSendWindowAvailable callback.
     */
    void send(ByteBuffer buffer, boolean finalChunk) {
      EnvoyHTTPStream stream = mStream.get();
      if (isAbandoned() || mEndStream ||
          !mCancelState.compareAndSet(CancelState.READY, CancelState.BUSY)) {
        return; // Cancelled - to late to send something.
      }
      // The Envoy Mobile library only cares about the capacity - must use the correct ByteBuffer
      buffer.flip();
      if (buffer.remaining() == buffer.capacity()) {
        stream.sendData(buffer, finalChunk);
      } else {
        ByteBuffer resizedBuffer = ByteBuffer.allocateDirect(buffer.remaining());
        resizedBuffer.put(buffer);
        stream.sendData(resizedBuffer, finalChunk);
      }
      if (!mCancelState.compareAndSet(CancelState.BUSY, CancelState.READY)) {
        stream.cancel();
      }
    }

    void readData(int size) {
      EnvoyHTTPStream stream = mStream.get();
      if (!mCancelState.compareAndSet(CancelState.READY, CancelState.BUSY)) {
        return; // Cancelled - to late to send something.
      }
      stream.readData(size);
      if (!mCancelState.compareAndSet(CancelState.BUSY, CancelState.READY)) {
        stream.cancel();
      }
    }

    /**
     * Cancels the Stream if the state permits - can be called by any Thread. Returns true is the
     * cancel was effectively sent.
     */
    void cancel() {
      EnvoyHTTPStream stream = mStream.get();
      if (this != mCronvoyCallbacks || mEndStream) {
        return;
      }
      @CancelState int oldState = mCancelState.getAndSet(CancelState.CANCELLED);
      if (oldState == CancelState.READY) {
        stream.cancel();
      }
    }

    private void setUrlResponseInfo(Map<String, List<String>> responseHeaders, int responseCode) {
      mAdditionalStatusDetails = Status.WAITING_FOR_RESPONSE;
      List<Map.Entry<String, String>> headerList = new ArrayList<>();
      String selectedTransport = "unknown";
      Set<Map.Entry<String, List<String>>> headers = responseHeaders.entrySet();

      for (Map.Entry<String, List<String>> headerEntry : headers) {
        String headerKey = headerEntry.getKey();
        if (headerEntry.getValue().get(0) == null) {
          continue;
        }
        if (X_ENVOY_UPSTREAM_ALPN.equals(headerKey)) {
          selectedTransport = headerEntry.getValue().get(0);
        }
        if (!headerKey.startsWith(X_ENVOY) && !headerKey.equals("date") &&
            !headerKey.equals(":status")) {
          for (String value : headerEntry.getValue()) {
            headerList.add(new SimpleEntry<>(headerKey, value));
          }
        }
      }
      // Important to copy the list here, because although we never concurrently modify
      // the list ourselves, user code might iterate over it while we're redirecting, and
      // that would throw ConcurrentModificationException.
      // TODO(https://github.com/envoyproxy/envoy-mobile/issues/1622) support proxy
      // TODO(https://github.com/envoyproxy/envoy-mobile/issues/1578) http caching
      mUrlResponseInfo.setResponseValues(
          new ArrayList<>(mUrlChain), responseCode, HttpReason.getReason(responseCode),
          Collections.unmodifiableList(headerList), false, selectedTransport, ":0");
    }

    private boolean completeAbandonIfAny(@State int originalState, @State int updatedState) {
      if (originalState == State.COMPLETE || originalState == State.CANCELLED ||
          originalState == State.ERROR) {
        return true;
      }
      if (originalState != State.PENDING_CANCEL && originalState != State.ERROR_PENDING_CANCEL) {
        return false;
      }
      fireCloseUploadDataProvider(); // Idempotent
      if (originalState == State.ERROR_PENDING_CANCEL && updatedState == State.ERROR) {
        onFailed();
        return true;
      }
      if (originalState == State.PENDING_CANCEL && updatedState == State.CANCELLED) {
        onCanceled();
        return true;
      }
      // Reaching here means that mEndStream == false.
      cancel();
      return true;
    }

    private boolean isAbandoned() {
      return this != mCronvoyCallbacks || mState.get() == State.AWAITING_FOLLOW_REDIRECT;
    }

    private boolean reportInternalStateTransitionErrorIfAny(@State int originalState,
                                                            @State int expectedOriginalState) {
      if (expectedOriginalState == originalState) {
        return false;
      }
      enterCronetErrorState(new IllegalStateException("Invalid state transition - expected " +
                                                      expectedOriginalState + " but was " +
                                                      originalState));
      return true;
    }
  }
}
