package org.chromium.net.impl;

import android.annotation.TargetApi;
import android.net.TrafficStats;
import android.os.Build;
import android.util.Log;
import androidx.annotation.GuardedBy;
import androidx.annotation.IntDef;
import androidx.annotation.Nullable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.channels.ReadableByteChannel;
import java.nio.channels.WritableByteChannel;
import java.util.AbstractMap.SimpleEntry;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import org.chromium.net.CronetException;
import org.chromium.net.InlineExecutionProhibitedException;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UrlResponseInfo;
import org.chromium.net.impl.Executors.CheckedRunnable;
import org.chromium.net.impl.Executors.DirectPreventingExecutor;

/**
 * Pure java UrlRequest, backed by {@link HttpURLConnection}.
 */
@TargetApi(Build.VERSION_CODES.ICE_CREAM_SANDWICH) // TrafficStats only available on ICS
final class JavaUrlRequest extends UrlRequestBase {

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
  private static final String TAG = JavaUrlRequest.class.getSimpleName();
  private static final int DEFAULT_CHUNK_LENGTH = JavaUploadDataSinkBase.DEFAULT_UPLOAD_BUFFER_SIZE;
  private static final String USER_AGENT = "User-Agent";
  private final AsyncUrlRequestCallback mCallbackAsync;
  private final Executor mExecutor;
  private final String mUserAgent;
  private final Map<String, String> mRequestHeaders = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
  private final List<String> mUrlChain = new ArrayList<>();
  /**
   * This is the source of thread safety in this class - no other synchronization is performed. By
   * compare-and-swapping from one state to another, we guarantee that operations aren't running
   * concurrently. Only the winner of a compare-and-swapping proceeds.
   *
   * <p>A caller can lose a compare-and-swapping for three reasons - user error (two calls to
   * read() without waiting for the read to succeed), runtime error (network code or user code
   * throws an exception), or cancellation.
   */
  private final AtomicInteger /* State */ mState = new AtomicInteger(State.NOT_STARTED);
  private final AtomicBoolean mUploadProviderClosed = new AtomicBoolean(false);

  private final boolean mAllowDirectExecutor;

  /* These don't change with redirects */
  private String mInitialMethod;
  private VersionSafeCallbacks.UploadDataProviderWrapper mUploadDataProvider;
  private Executor mUploadExecutor;

  /**
   * Holds a subset of StatusValues - {@link State#STARTED} can represent {@link
   * Status#SENDING_REQUEST} or {@link Status#WAITING_FOR_RESPONSE}. While the distinction isn't
   * needed to implement the logic in this class, it is needed to implement {@link
   * #getStatus(StatusListener)}.
   *
   * <p>Concurrency notes - this value is not atomically updated with mState, so there is some
   * risk that we'd get an inconsistent snapshot of both - however, it also happens that this value
   * is only used with the STARTED state, so it's inconsequential.
   */
  @StatusValues private volatile int mAdditionalStatusDetails = Status.INVALID;

  /* These change with redirects. */
  private String mCurrentUrl;
  @Nullable private ReadableByteChannel mResponseChannel; // Only accessed on mExecutor.
  private UrlResponseInfoImpl mUrlResponseInfo;
  private String mPendingRedirectUrl;
  private HttpURLConnection mCurrentUrlConnection;    // Only accessed on mExecutor.
  private OutputStreamDataSink mOutputStreamDataSink; // Only accessed on mExecutor.

  /**
   * @param executor The executor used for reading and writing from sockets
   * @param userExecutor The executor used to dispatch to {@code callback}
   */
  JavaUrlRequest(Callback callback, final Executor executor, Executor userExecutor, String url,
                 String userAgent, boolean allowDirectExecutor, boolean trafficStatsTagSet,
                 int trafficStatsTag, final boolean trafficStatsUidSet, final int trafficStatsUid) {
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

    this.mAllowDirectExecutor = allowDirectExecutor;
    this.mCallbackAsync = new AsyncUrlRequestCallback(callback, userExecutor);
    final int trafficStatsTagToUse =
        trafficStatsTagSet ? trafficStatsTag : TrafficStats.getThreadStatsTag();
    this.mExecutor = new SerializingExecutor(new Executor() {
      @Override
      public void execute(final Runnable command) {
        executor.execute(new Runnable() {
          @Override
          public void run() {
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
          }
        });
      }
    });
    this.mCurrentUrl = url;
    this.mUserAgent = userAgent;
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
    if (mRequestHeaders.containsKey(header)) {
      mRequestHeaders.remove(header);
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
    this.mUploadDataProvider =
        new VersionSafeCallbacks.UploadDataProviderWrapper(uploadDataProvider);
    if (mAllowDirectExecutor) {
      this.mUploadExecutor = executor;
    } else {
      this.mUploadExecutor = new DirectPreventingExecutor(executor);
    }
  }

  private final class OutputStreamDataSink extends JavaUploadDataSinkBase {

    private final HttpURLConnection mUrlConnection;
    private final AtomicBoolean mOutputChannelClosed = new AtomicBoolean(false);
    private WritableByteChannel mOutputChannel;
    private OutputStream mUrlConnectionOutputStream;

    OutputStreamDataSink(final Executor userExecutor, Executor executor,
                         HttpURLConnection urlConnection,
                         VersionSafeCallbacks.UploadDataProviderWrapper provider) {
      super(userExecutor, executor, provider);
      mUrlConnection = urlConnection;
    }

    @Override
    protected void initializeRead() throws IOException {
      if (mOutputChannel == null) {
        mAdditionalStatusDetails = Status.CONNECTING;
        mUrlConnection.setDoOutput(true);
        mUrlConnection.connect();
        mAdditionalStatusDetails = Status.SENDING_REQUEST;
        mUrlConnectionOutputStream = mUrlConnection.getOutputStream();
        mOutputChannel = Channels.newChannel(mUrlConnectionOutputStream);
      }
    }

    void closeOutputChannel() throws IOException {
      if (mOutputChannel != null && mOutputChannelClosed.compareAndSet(
                                        /* expected= */ false, /* updated= */ true)) {
        mOutputChannel.close();
      }
    }

    @Override
    protected void finish() throws IOException {
      closeOutputChannel();
      fireGetHeaders();
    }

    @Override
    protected void initializeStart(long totalBytes) {
      if (totalBytes > 0 && totalBytes <= Integer.MAX_VALUE) {
        mUrlConnection.setFixedLengthStreamingMode((int)totalBytes);
      } else if (totalBytes > Integer.MAX_VALUE &&
                 Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
        mUrlConnection.setFixedLengthStreamingMode(totalBytes);
      } else {
        // Even if we know the length, but we're running pre-kitkat and it's larger
        // than an int can hold, we have to use chunked - otherwise we'll end up
        // buffering the whole response in memory.
        mUrlConnection.setChunkedStreamingMode(DEFAULT_CHUNK_LENGTH);
      }
    }

    @Override
    protected int processSuccessfulRead(ByteBuffer buffer) throws IOException {
      int totalBytesProcessed = 0;
      while (buffer.hasRemaining()) {
        totalBytesProcessed += mOutputChannel.write(buffer);
      }
      // Forces a chunk to be sent, rather than buffering to the DEFAULT_CHUNK_LENGTH.
      // This allows clients to trickle-upload bytes as they become available without
      // introducing latency due to buffering.
      mUrlConnectionOutputStream.flush();
      return totalBytesProcessed;
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
    mAdditionalStatusDetails = Status.CONNECTING;
    transitionStates(State.NOT_STARTED, State.STARTED, new Runnable() {
      @Override
      public void run() {
        mUrlChain.add(mCurrentUrl);
        fireOpenConnection();
      }
    });
  }

  private void enterErrorState(final CronetException error) {
    if (setTerminalState(State.ERROR)) {
      fireDisconnect();
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
        if (mState.compareAndSet(/* expected= */ oldState, /* updated= */ error)) {
          return true;
        }
      }
      }
    }
  }

  /**
   * Ends the request with an error, caused by an exception thrown from user code.
   */
  private void enterUserErrorState(final Throwable error) {
    enterErrorState(
        new CallbackExceptionImpl("Exception received from UrlRequest.Callback", error));
  }

  /**
   * Ends the request with an error, caused by an exception thrown from user code.
   */
  private void enterUploadErrorState(final Throwable error) {
    enterErrorState(new CallbackExceptionImpl("Exception received from UploadDataProvider", error));
  }

  private void enterCronetErrorState(final Throwable error) {
    // TODO(clm) mapping from Java exception (UnknownHostException, for example) to net error
    // code goes here.
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
    transitionStates(State.AWAITING_FOLLOW_REDIRECT, State.STARTED, new Runnable() {
      @Override
      public void run() {
        mCurrentUrl = mPendingRedirectUrl;
        mPendingRedirectUrl = null;
        fireOpenConnection();
      }
    });
  }

  private void fireGetHeaders() {
    mAdditionalStatusDetails = Status.WAITING_FOR_RESPONSE;
    mExecutor.execute(errorSetting(new CheckedRunnable() {
      @Override
      public void run() throws Exception {
        if (mCurrentUrlConnection == null) {
          return; // We've been cancelled
        }
        final List<Map.Entry<String, String>> headerList = new ArrayList<>();
        String selectedTransport = "http/1.1";
        String headerValue;
        for (int i = 0; (headerValue = mCurrentUrlConnection.getHeaderField(i)) != null; i++) {
          String headerKey = mCurrentUrlConnection.getHeaderFieldKey(i);
          if (headerKey != null) {
            if (X_ANDROID_SELECTED_TRANSPORT.equalsIgnoreCase(headerKey)) {
              selectedTransport = headerValue;
            }
            if (!headerKey.startsWith(X_ANDROID)) {
              headerList.add(new SimpleEntry<>(headerKey.toLowerCase(), headerValue));
            }
          }
        }

        int responseCode = mCurrentUrlConnection.getResponseCode();
        // Important to copy the list here, because although we never concurrently modify
        // the list ourselves, user code might iterate over it while we're redirecting, and
        // that would throw ConcurrentModificationException.
        mUrlResponseInfo = new UrlResponseInfoImpl(
            new ArrayList<>(mUrlChain), responseCode, mCurrentUrlConnection.getResponseMessage(),
            Collections.unmodifiableList(headerList), false, selectedTransport, "", 0);
        // TODO(clm) actual redirect handling? post -> get and whatnot?
        if (responseCode >= 300 && responseCode < 400) {
          List<String> locationFields = mUrlResponseInfo.getAllHeaders().get("location");
          if (locationFields != null) {
            fireRedirectReceived(locationFields.get(0));
            return;
          }
        }
        fireCloseUploadDataProvider();
        if (responseCode >= 400) {
          InputStream inputStream = mCurrentUrlConnection.getErrorStream();
          mResponseChannel = inputStream == null ? null : InputStreamChannel.wrap(inputStream);
          mCallbackAsync.onResponseStarted(mUrlResponseInfo);
        } else {
          mResponseChannel = InputStreamChannel.wrap(mCurrentUrlConnection.getInputStream());
          mCallbackAsync.onResponseStarted(mUrlResponseInfo);
        }
      }
    }));
  }

  private void fireCloseUploadDataProvider() {
    if (mUploadDataProvider != null && mUploadProviderClosed.compareAndSet(
                                           /* expected= */ false, /* updated= */ true)) {
      try {
        mUploadExecutor.execute(uploadErrorSetting(new CheckedRunnable() {
          @Override
          public void run() throws Exception {
            mUploadDataProvider.close();
          }
        }));
      } catch (RejectedExecutionException e) {
        Log.e(TAG, "Exception when closing uploadDataProvider", e);
      }
    }
  }

  private void fireRedirectReceived(final String locationField) {
    transitionStates(State.STARTED, State.REDIRECT_RECEIVED, new Runnable() {
      @Override
      public void run() {
        mPendingRedirectUrl = URI.create(mCurrentUrl).resolve(locationField).toString();
        mUrlChain.add(mPendingRedirectUrl);
        transitionStates(State.REDIRECT_RECEIVED, State.AWAITING_FOLLOW_REDIRECT, new Runnable() {
          @Override
          public void run() {
            mCallbackAsync.onRedirectReceived(mUrlResponseInfo, mPendingRedirectUrl);
          }
        });
      }
    });
  }

  private void fireOpenConnection() {
    mExecutor.execute(errorSetting(new CheckedRunnable() {
      @Override
      public void run() throws Exception {
        // If we're cancelled, then our old connection will be disconnected for us and
        // we shouldn't open a new one.
        if (mState.get() == State.CANCELLED) {
          return;
        }

        final URL url = new URL(mCurrentUrl);
        if (mCurrentUrlConnection != null) {
          mCurrentUrlConnection.disconnect();
          mCurrentUrlConnection = null;
        }
        mCurrentUrlConnection = (HttpURLConnection)url.openConnection();
        mCurrentUrlConnection.setInstanceFollowRedirects(false);
        if (!mRequestHeaders.containsKey(USER_AGENT)) {
          mRequestHeaders.put(USER_AGENT, mUserAgent);
        }
        for (Map.Entry<String, String> entry : mRequestHeaders.entrySet()) {
          mCurrentUrlConnection.setRequestProperty(entry.getKey(), entry.getValue());
        }
        if (mInitialMethod == null) {
          mInitialMethod = "GET";
        }
        mCurrentUrlConnection.setRequestMethod(mInitialMethod);
        if (mUploadDataProvider != null) {
          mOutputStreamDataSink = new OutputStreamDataSink(
              mUploadExecutor, mExecutor, mCurrentUrlConnection, mUploadDataProvider);
          mOutputStreamDataSink.start(mUrlChain.size() == 1);
        } else {
          mAdditionalStatusDetails = Status.CONNECTING;
          mCurrentUrlConnection.connect();
          fireGetHeaders();
        }
      }
    }));
  }

  private Runnable errorSetting(final CheckedRunnable delegate) {
    return new Runnable() {
      @Override
      public void run() {
        try {
          delegate.run();
        } catch (Throwable t) {
          enterCronetErrorState(t);
        }
      }
    };
  }

  private Runnable userErrorSetting(final CheckedRunnable delegate) {
    return new Runnable() {
      @Override
      public void run() {
        try {
          delegate.run();
        } catch (Throwable t) {
          enterUserErrorState(t);
        }
      }
    };
  }

  private Runnable uploadErrorSetting(final CheckedRunnable delegate) {
    return new Runnable() {
      @Override
      public void run() {
        try {
          delegate.run();
        } catch (Throwable t) {
          enterUploadErrorState(t);
        }
      }
    };
  }

  @Override
  public void read(final ByteBuffer buffer) {
    Preconditions.checkDirect(buffer);
    Preconditions.checkHasRemaining(buffer);
    transitionStates(State.AWAITING_READ, State.READING, new Runnable() {
      @Override
      public void run() {
        mExecutor.execute(errorSetting(new CheckedRunnable() {
          @Override
          public void run() throws Exception {
            int read = mResponseChannel == null ? -1 : mResponseChannel.read(buffer);
            processReadResult(read, buffer);
          }
        }));
      }
    });
  }

  private void processReadResult(int read, final ByteBuffer buffer) throws IOException {
    if (read != -1) {
      mCallbackAsync.onReadCompleted(mUrlResponseInfo, buffer);
    } else {
      if (mResponseChannel != null) {
        mResponseChannel.close();
      }
      if (mState.compareAndSet(
              /* expected= */ State.READING, /* updated= */ State.COMPLETE)) {
        fireDisconnect();
        mCallbackAsync.onSucceeded(mUrlResponseInfo);
      }
    }
  }

  private void fireDisconnect() {
    mExecutor.execute(new Runnable() {
      @Override
      public void run() {
        if (mOutputStreamDataSink != null) {
          try {
            mOutputStreamDataSink.closeOutputChannel();
          } catch (IOException e) {
            Log.e(TAG, "Exception when closing OutputChannel", e);
          }
        }
        if (mCurrentUrlConnection != null) {
          mCurrentUrlConnection.disconnect();
          mCurrentUrlConnection = null;
        }
      }
    });
  }

  @Override
  public void cancel() {
    @State int oldState = mState.getAndSet(State.CANCELLED);
    switch (oldState) {
    // We've just scheduled some user code to run. When they perform their next operation,
    // they'll observe it and fail. However, if user code is cancelling in response to one
    // of these callbacks, we'll never actually cancel!
    // TODO(clm) figure out if it's possible to avoid concurrency in user callbacks.
    case State.REDIRECT_RECEIVED:
    case State.AWAITING_FOLLOW_REDIRECT:
    case State.AWAITING_READ:

      // User code is waiting on us - cancel away!
    case State.STARTED:
    case State.READING:
      fireDisconnect();
      fireCloseUploadDataProvider();
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
    int extraStatus = this.mAdditionalStatusDetails;

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

  /**
   * This wrapper ensures that callbacks are always called on the correct executor
   */
  private final class AsyncUrlRequestCallback {

    final VersionSafeCallbacks.UrlRequestCallback mCallback;
    final Executor mUserExecutor;
    final Executor mFallbackExecutor;

    AsyncUrlRequestCallback(Callback callback, final Executor userExecutor) {
      this.mCallback = new VersionSafeCallbacks.UrlRequestCallback(callback);
      if (mAllowDirectExecutor) {
        this.mUserExecutor = userExecutor;
        this.mFallbackExecutor = null;
      } else {
        mUserExecutor = new DirectPreventingExecutor(userExecutor);
        mFallbackExecutor = userExecutor;
      }
    }

    void sendStatus(final VersionSafeCallbacks.UrlRequestStatusListener listener,
                    final int status) {
      mUserExecutor.execute(new Runnable() {
        @Override
        public void run() {
          listener.onStatus(status);
        }
      });
    }

    void execute(CheckedRunnable runnable) {
      try {
        mUserExecutor.execute(userErrorSetting(runnable));
      } catch (RejectedExecutionException e) {
        enterErrorState(new CronetExceptionImpl("Exception posting task to executor", e));
      }
    }

    void onRedirectReceived(final UrlResponseInfo info, final String newLocationUrl) {
      execute(new CheckedRunnable() {
        @Override
        public void run() throws Exception {
          mCallback.onRedirectReceived(JavaUrlRequest.this, info, newLocationUrl);
        }
      });
    }

    void onResponseStarted(UrlResponseInfo info) {
      execute(new CheckedRunnable() {
        @Override
        public void run() throws Exception {
          if (mState.compareAndSet(/* expected= */ State.STARTED,
                                   /* updated= */ State.AWAITING_READ)) {
            mCallback.onResponseStarted(JavaUrlRequest.this, mUrlResponseInfo);
          }
        }
      });
    }

    void onReadCompleted(final UrlResponseInfo info, final ByteBuffer byteBuffer) {
      execute(new CheckedRunnable() {
        @Override
        public void run() throws Exception {
          if (mState.compareAndSet(/* expected= */ State.READING,
                                   /* updated= */ State.AWAITING_READ)) {
            mCallback.onReadCompleted(JavaUrlRequest.this, info, byteBuffer);
          }
        }
      });
    }

    void onCanceled(final UrlResponseInfo info) {
      closeResponseChannel();
      mUserExecutor.execute(new Runnable() {
        @Override
        public void run() {
          try {
            mCallback.onCanceled(JavaUrlRequest.this, info);
          } catch (Exception exception) {
            Log.e(TAG, "Exception in onCanceled method", exception);
          }
        }
      });
    }

    void onSucceeded(final UrlResponseInfo info) {
      mUserExecutor.execute(new Runnable() {
        @Override
        public void run() {
          try {
            mCallback.onSucceeded(JavaUrlRequest.this, info);
          } catch (Exception exception) {
            Log.e(TAG, "Exception in onSucceeded method", exception);
          }
        }
      });
    }

    void onFailed(final UrlResponseInfo urlResponseInfo, final CronetException e) {
      closeResponseChannel();
      Runnable runnable = new Runnable() {
        @Override
        public void run() {
          try {
            mCallback.onFailed(JavaUrlRequest.this, urlResponseInfo, e);
          } catch (Exception exception) {
            Log.e(TAG, "Exception in onFailed method", exception);
          }
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

  private void closeResponseChannel() {
    mExecutor.execute(new Runnable() {
      @Override
      public void run() {
        if (mResponseChannel != null) {
          try {
            mResponseChannel.close();
          } catch (IOException e) {
            // Silently ignored, willingly.
          }
          mResponseChannel = null;
        }
      }
    });
  }

  // Executor that runs one task at a time on an underlying Executor.
  // NOTE: Do not use to wrap user supplied Executor as lock is held while underlying execute()
  // is called.
  private static final class SerializingExecutor implements Executor {

    private final Executor mUnderlyingExecutor;
    private final Runnable mRunTasks = new Runnable() {
      @Override
      public void run() {
        Runnable task;
        synchronized (mTaskQueue) {
          if (mRunning) {
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
    // Indicates if mRunTasks is actively running tasks. Synchronized on mTaskQueue.
    @GuardedBy("mTaskQueue") private boolean mRunning;

    SerializingExecutor(Executor underlyingExecutor) { mUnderlyingExecutor = underlyingExecutor; }

    @Override
    public void execute(Runnable command) {
      synchronized (mTaskQueue) {
        mTaskQueue.addLast(command);
        try {
          mUnderlyingExecutor.execute(mRunTasks);
        } catch (RejectedExecutionException e) {
          // If shutting down, do not add new tasks to the queue.
          mTaskQueue.removeLast();
        }
      }
    }
  }
}
