package org.cronvoy;

import android.annotation.TargetApi;
import android.net.TrafficStats;
import android.os.Build;
import android.util.Log;
import androidx.annotation.GuardedBy;
import io.envoyproxy.envoymobile.RequestHeaders;
import io.envoyproxy.envoymobile.RequestHeadersBuilder;
import io.envoyproxy.envoymobile.RequestMethod;
import io.envoyproxy.envoymobile.ResponseHeaders;
import io.envoyproxy.envoymobile.Stream;
import io.envoyproxy.envoymobile.UpstreamHttpProtocol;
import java.net.URI;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.AbstractMap.SimpleEntry;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.chromium.net.CronetException;
import org.chromium.net.InlineExecutionProhibitedException;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UrlResponseInfo;
import org.cronvoy.Annotations.State;
import org.cronvoy.Annotations.StatusValues;
import org.cronvoy.Executors.CheckedRunnable;
import org.cronvoy.Executors.DirectPreventingExecutor;

/** UrlRequest, backed by Envoy-Mobile. */
@TargetApi(Build.VERSION_CODES.ICE_CREAM_SANDWICH) // TrafficStats only available on ICS
final class CronvoyUrlRequest extends UrlRequestBase {

  private static final String X_ANDROID = "X-Android";
  private static final String X_ANDROID_SELECTED_TRANSPORT = "X-Android-Selected-Transport";
  private static final String TAG = CronvoyUrlRequest.class.getSimpleName();
  private static final String USER_AGENT = "User-Agent";

  private final AsyncUrlRequestCallback callbackAsync;
  private final PausableSerializingExecutor cronvoyExecutor;
  private final PausableSerializingExecutor envoyCallbackExecutor;
  private final String mUserAgent;
  private final Map<String, String> requestHeaders = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
  private final List<String> urlChain = new ArrayList<>();
  private final CronvoyEngine cronvoyEngine;

  /**
   * This is the source of thread safety in this class - no other synchronization is performed. By
   * compare-and-swapping from one state to another, we guarantee that operations aren't running
   * concurrently. Only the winner of a compare-and-swapping proceeds.
   *
   * <p>A caller can lose a compare-and-swapping for three reasons - user error (two calls to read()
   * without waiting for the read to succeed), runtime error (network code or user code throws an
   * exception), or cancellation.
   */
  private final AtomicInteger /* State */ state = new AtomicInteger(State.NOT_STARTED);

  private final AtomicBoolean uploadProviderClosed = new AtomicBoolean(false);

  private final boolean allowDirectExecutor;

  /* These don't change with redirects */
  private String initialMethod;
  private VersionSafeCallbacks.UploadDataProviderWrapper uploadDataProvider;
  private Executor uploadExecutor;
  private Stream stream;
  private boolean endStream;
  private final AtomicBoolean cancelCalled = new AtomicBoolean();

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
  @StatusValues private volatile int additionalStatusDetails = Status.INVALID;

  /* These change with redirects. */
  private String currentUrl;
  private UrlResponseInfoImpl urlResponseInfo;
  private String pendingRedirectUrl;
  private final AtomicReference<ByteBuffer> mostRecentBufferRead = new AtomicReference<>();
  private final AtomicReference<ByteBuffer> userCurrentReadBuffer = new AtomicReference<>();
  private OutputStreamDataSink outputStreamDataSink;

  /**
   * @param executor The executor for orchestrating tasks between envoy-mobile callbacks
   * @param userExecutor The executor used to dispatch to Cronet {@code callback}
   */
  CronvoyUrlRequest(CronvoyEngine cronvoyEngine, Callback callback, final Executor executor,
                    Executor userExecutor, String url, String userAgent,
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

    this.cronvoyEngine = cronvoyEngine;
    this.allowDirectExecutor = allowDirectExecutor;
    this.callbackAsync = new AsyncUrlRequestCallback(callback, userExecutor);
    final int trafficStatsTagToUse =
        trafficStatsTagSet ? trafficStatsTag : TrafficStats.getThreadStatsTag();
    this.cronvoyExecutor = createSerializedExecutor(executor, trafficStatsUidSet, trafficStatsUid,
                                                    trafficStatsTagToUse);
    this.envoyCallbackExecutor = createSerializedExecutor(executor, trafficStatsUidSet,
                                                          trafficStatsUid, trafficStatsTagToUse);
    this.currentUrl = url;
    this.mUserAgent = userAgent;
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
      initialMethod = method;
    } else {
      throw new IllegalArgumentException("Invalid http method " + method);
    }
  }

  private void checkNotStarted() {
    @State int state = this.state.get();
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
    requestHeaders.put(header, value);
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
    if (!requestHeaders.containsKey("Content-Type")) {
      throw new IllegalArgumentException("Requests with upload data must have a Content-Type.");
    }
    checkNotStarted();
    if (initialMethod == null) {
      initialMethod = "POST";
    }
    this.uploadDataProvider =
        new VersionSafeCallbacks.UploadDataProviderWrapper(uploadDataProvider);
    if (allowDirectExecutor) {
      this.uploadExecutor = executor;
    } else {
      this.uploadExecutor = new DirectPreventingExecutor(executor);
    }
  }

  private final class OutputStreamDataSink extends CronvoyUploadDataSinkBase {

    OutputStreamDataSink() { super(uploadExecutor, cronvoyExecutor, uploadDataProvider); }

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
        stream.close(buffer);
      } else {
        stream.sendData(buffer);
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
      cronvoyExecutor.pause();
      cronvoyEngine.setTaskToExecuteWhenInitializationIsCompleted(cronvoyExecutor::resume);
      additionalStatusDetails = Status.CONNECTING;
      urlChain.add(currentUrl);
      fireOpenConnection();
    });
  }

  private void enterErrorState(final CronetException error) {
    if (setTerminalState(State.ERROR)) {
      if (cancelCalled.compareAndSet(false, true)) {
        stream.cancel();
      }
      fireCloseUploadDataProvider();
      callbackAsync.onFailed(urlResponseInfo, error);
    }
  }

  private boolean setTerminalState(@State int error) {
    while (true) {
      @State int oldState = state.get();
      switch (oldState) {
      case State.NOT_STARTED:
        throw new IllegalStateException("Can't enter error state before start");
      case State.ERROR:    // fallthrough
      case State.COMPLETE: // fallthrough
      case State.CANCELLED:
        return false; // Already in a terminal state
      default: {
        if (state.compareAndSet(/* expect= */ oldState, /* update= */ error)) {
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
    if (!state.compareAndSet(expected, newState)) {
      @State int state = this.state.get();
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
      currentUrl = pendingRedirectUrl;
      pendingRedirectUrl = null;
      fireOpenConnection();
    });
  }

  private void onResponseHeaders(ResponseHeaders responseHeaders, boolean lastCallback) {
    additionalStatusDetails = Status.WAITING_FOR_RESPONSE;
    if (state.get() == State.CANCELLED) {
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
    urlResponseInfo = new UrlResponseInfoImpl(
        new ArrayList<>(urlChain), responseCode,
        "HTTP " + responseHeaders.getHttpStatus(), // UrlConnection.getResponseMessage(),
        Collections.unmodifiableList(headerList), false, selectedTransport, "", 0);
    // TODO(carloseltuerto) make this "if" a possibility with Envoy-Mobile
    if (responseCode >= 300 && responseCode < 400) {
      List<String> locationFields = urlResponseInfo.getAllHeaders().get("location");
      if (locationFields != null) {
        fireRedirectReceived(locationFields.get(0));
        return;
      }
    }
    fireCloseUploadDataProvider();
    endStream = lastCallback;
    // There is no "body" data: fake an empty response to trigger the Cronet next step.
    if (endStream) {
      // By contract, envoy-mobile won't send more "callbacks".
      mostRecentBufferRead.set(ByteBuffer.allocateDirect(0));
    }
    callbackAsync.onResponseStarted(urlResponseInfo);
  }

  private void fireCloseUploadDataProvider() {
    if (uploadDataProvider != null &&
        uploadProviderClosed.compareAndSet(/* expect= */ false, /* update= */ true)) {
      try {
        uploadExecutor.execute(uploadErrorSetting(uploadDataProvider::close));
      } catch (RejectedExecutionException e) {
        Log.e(TAG, "Exception when closing uploadDataProvider", e);
      }
    }
  }

  private void fireRedirectReceived(final String locationField) {
    transitionStates(State.STARTED, State.REDIRECT_RECEIVED, () -> {
      pendingRedirectUrl = URI.create(currentUrl).resolve(locationField).toString();
      urlChain.add(pendingRedirectUrl);
      transitionStates(State.REDIRECT_RECEIVED, State.AWAITING_FOLLOW_REDIRECT,
                       () -> callbackAsync.onRedirectReceived(urlResponseInfo, pendingRedirectUrl));
    });
  }

  private void fireOpenConnection() {
    cronvoyExecutor.execute(errorSetting(() -> {
      // If we're cancelled, then our old connection will be disconnected for us and
      // we shouldn't open a new one.
      if (state.get() == State.CANCELLED) {
        return;
      }
      final URL url = new URL(currentUrl);
      if (initialMethod == null) {
        initialMethod = "GET";
      }
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
      UpstreamHttpProtocol protocol =
          cronvoyEngine.getBuilder().http2Enabled() && url.getProtocol().equalsIgnoreCase("https")
              ? UpstreamHttpProtocol.HTTP2
              : UpstreamHttpProtocol.HTTP1;
      RequestHeaders requestHeaders =
          requestHeadersBuilder.addUpstreamHttpProtocol(protocol).build();

      stream = cronvoyEngine.getEnvoyEngine()
                   .streamClient()
                   .newStreamPrototype()
                   .setOnResponseHeaders((responseHeaders, lastCallback) -> {
                     onResponseHeaders(responseHeaders, lastCallback);
                     return null;
                   })
                   .setOnResponseData((data, lastCallback) -> {
                     envoyCallbackExecutor.pause();
                     endStream = lastCallback;
                     if (!mostRecentBufferRead.compareAndSet(null, data)) {
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
                     cronvoyExecutor.execute(() -> enterCronetErrorState(throwable));
                     return null;
                   })
                   .setOnCancel(() -> {
                     cancelCalled.set(true);
                     cancel();
                     return null;
                   })
                   .start(envoyCallbackExecutor)
                   .sendHeaders(requestHeaders, uploadDataProvider == null);
      if (uploadDataProvider != null) {
        outputStreamDataSink = new OutputStreamDataSink();
        outputStreamDataSink.start(/* firstTime= */ urlChain.size() == 1);
      }
    }));
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
                     () -> cronvoyExecutor.execute(errorSetting(() -> {
                       if (!userCurrentReadBuffer.compareAndSet(null, buffer)) {
                         throw new IllegalStateException("userCurrentReadBuffer should be clear");
                       }
                       processReadResult();
                     })));
  }

  private void processReadResult() {
    ByteBuffer sourceBuffer = mostRecentBufferRead.get();
    if (sourceBuffer == null) {
      return;
    }
    ByteBuffer sinkBuffer = userCurrentReadBuffer.getAndSet(null);
    if (sinkBuffer == null) {
      return;
    }
    while (sinkBuffer.hasRemaining() && sourceBuffer.hasRemaining()) {
      sinkBuffer.put(sourceBuffer.get());
    }
    if (sourceBuffer.hasRemaining() || !endStream) {
      callbackAsync.onReadCompleted(urlResponseInfo, sinkBuffer);
      return;
    }
    if (state.compareAndSet(/* expect= */ State.READING,
                            /* update= */ State.COMPLETE)) {
      callbackAsync.onSucceeded(urlResponseInfo);
    }
  }

  @Override
  public void cancel() {
    @State int oldState = state.getAndSet(State.CANCELLED);
    switch (oldState) {
    case State.REDIRECT_RECEIVED:
    case State.AWAITING_FOLLOW_REDIRECT:
    case State.AWAITING_READ:
    case State.STARTED:
    case State.READING:
      fireCloseUploadDataProvider();
      if (stream != null && cancelCalled.compareAndSet(false, true)) {
        stream.cancel();
      }
      callbackAsync.onCanceled(urlResponseInfo);
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
    @State int state = this.state.get();
    return state == State.COMPLETE || state == State.ERROR || state == State.CANCELLED;
  }

  @Override
  public void getStatus(StatusListener listener) {
    @State int state = this.state.get();
    int extraStatus = this.additionalStatusDetails;

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

    callbackAsync.sendStatus(new VersionSafeCallbacks.UrlRequestStatusListener(listener), status);
  }

  /** This wrapper ensures that callbacks are always called on the correct executor */
  private class AsyncUrlRequestCallback {
    final VersionSafeCallbacks.UrlRequestCallback callback;
    final Executor userExecutor;
    final Executor fallbackExecutor;

    AsyncUrlRequestCallback(Callback callback, final Executor userExecutor) {
      this.callback = new VersionSafeCallbacks.UrlRequestCallback(callback);
      if (allowDirectExecutor) {
        this.userExecutor = userExecutor;
        this.fallbackExecutor = null;
      } else {
        this.userExecutor = new DirectPreventingExecutor(userExecutor);
        fallbackExecutor = userExecutor;
      }
    }

    void sendStatus(final VersionSafeCallbacks.UrlRequestStatusListener listener,
                    final int status) {
      userExecutor.execute(() -> listener.onStatus(status));
    }

    void execute(CheckedRunnable runnable) {
      try {
        userExecutor.execute(userErrorSetting(runnable));
      } catch (RejectedExecutionException e) {
        enterErrorState(new CronetExceptionImpl("Exception posting task to executor", e));
      }
    }

    void onRedirectReceived(final UrlResponseInfo info, final String newLocationUrl) {
      execute(() -> callback.onRedirectReceived(CronvoyUrlRequest.this, info, newLocationUrl));
    }

    void onResponseStarted(UrlResponseInfo info) {
      execute(() -> {
        if (state.compareAndSet(
                /* expect= */ State.STARTED,
                /* update= */ State.AWAITING_READ)) {
          callback.onResponseStarted(CronvoyUrlRequest.this, info);
        }
      });
    }

    void onReadCompleted(final UrlResponseInfo info, final ByteBuffer byteBuffer) {
      execute(() -> {
        if (state.compareAndSet(
                /* expect= */ State.READING,
                /* update= */ State.AWAITING_READ)) {
          callback.onReadCompleted(CronvoyUrlRequest.this, info, byteBuffer);
          if (!mostRecentBufferRead.get().hasRemaining()) {
            mostRecentBufferRead.set(null);
            envoyCallbackExecutor.resume();
          }
        }
      });
    }

    void onCanceled(final UrlResponseInfo info) {
      userExecutor.execute(() -> {
        try {
          callback.onCanceled(CronvoyUrlRequest.this, info);
        } catch (Exception exception) {
          Log.e(TAG, "Exception in onCanceled method", exception);
        }
      });
    }

    void onSucceeded(final UrlResponseInfo info) {
      userExecutor.execute(() -> {
        try {
          callback.onSucceeded(CronvoyUrlRequest.this, info);
        } catch (Exception exception) {
          Log.e(TAG, "Exception in onSucceeded method", exception);
        }
      });
    }

    void onFailed(final UrlResponseInfo urlResponseInfo, final CronetException e) {
      Runnable runnable = () -> {
        try {
          callback.onFailed(CronvoyUrlRequest.this, urlResponseInfo, e);
        } catch (Exception exception) {
          Log.e(TAG, "Exception in onFailed method", exception);
        }
      };
      try {
        userExecutor.execute(runnable);
      } catch (InlineExecutionProhibitedException wasDirect) {
        if (fallbackExecutor != null) {
          fallbackExecutor.execute(runnable);
        }
      }
    }
  }

  // Executor that runs one task at a time on an underlying Executor. It can be paused/resumed.
  // NOTE: Do not use to wrap user supplied Executor as lock is held while underlying execute()
  // is called.
  private static final class PausableSerializingExecutor implements Executor {

    private final Executor underlyingExecutor;
    private final Runnable runTasks = new Runnable() {
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
                  underlyingExecutor.execute(runTasks);
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
    @GuardedBy("mTaskQueue") private final ArrayDeque<Runnable> mTaskQueue = new ArrayDeque<>();
    // Indicates if mRunTasks is actively running tasks. Synchronized on mTaskQueue.
    @GuardedBy("mTaskQueue") private boolean mRunning;
    @GuardedBy("mTaskQueue") private boolean mPaused;

    PausableSerializingExecutor(Executor underlyingExecutor) {
      this.underlyingExecutor = underlyingExecutor;
    }

    void pause() {
      synchronized (mTaskQueue) { mPaused = true; }
    }

    void resume() {
      synchronized (mTaskQueue) {
        mPaused = false;
        if (!mTaskQueue.isEmpty()) {
          try {
            underlyingExecutor.execute(runTasks);
          } catch (RejectedExecutionException e) {
            // Ignoring is fine here - this is shutting down.
          }
        }
      }
    }

    @Override
    public void execute(Runnable command) {
      synchronized (mTaskQueue) {
        mTaskQueue.addLast(command);
        if (!mPaused && !mTaskQueue.isEmpty()) {
          try {
            underlyingExecutor.execute(runTasks);
          } catch (RejectedExecutionException e) {
            // Shutting down, do not add new task to the queue.
            mTaskQueue.removeLast();
          }
        }
      }
    }
  }
}
