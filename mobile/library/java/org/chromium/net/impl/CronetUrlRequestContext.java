package org.chromium.net.impl;

import static android.os.Process.THREAD_PRIORITY_BACKGROUND;
import static android.os.Process.THREAD_PRIORITY_MORE_FAVORABLE;

import android.os.ConditionVariable;
import androidx.annotation.GuardedBy;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import java.io.IOException;
import java.net.Proxy;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandlerFactory;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.chromium.net.BidirectionalStream;
import org.chromium.net.ExperimentalBidirectionalStream;
import org.chromium.net.NetworkQualityRttListener;
import org.chromium.net.NetworkQualityThroughputListener;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UrlRequest;
import org.chromium.net.impl.VersionSafeCallbacks.RequestFinishedInfoListener;
import org.chromium.net.urlconnection.CronetHttpURLConnection;
import org.chromium.net.urlconnection.CronetURLStreamHandlerFactory;

/**
 * Cronvoy engine shim.
 *
 * <p>Does not support yet netlogs, transferred data measurement, bidistream, cache, or priority.
 */
public final class CronetUrlRequestContext extends CronetEngineBase {

  static final String LOG_TAG = CronetUrlRequestContext.class.getSimpleName();

  /**
   * Synchronize access to mUrlRequestContextAdapter and shutdown routine.
   */
  private final Object mLock = new Object();
  private final ConditionVariable mInitCompleted = new ConditionVariable(false);
  private final AtomicInteger mActiveRequestCount = new AtomicInteger(0);

  @GuardedBy("mLock") private Engine mEngine;
  /**
   * This field is accessed without synchronization, but only for the purposes of reference
   * equality comparison with other threads. If such a comparison is performed on the network
   * thread, then there is a happens-before edge between the write of this field and the
   * subsequent read; if it's performed on another thread, then observing a value of null won't
   * change the result of the comparison.
   */
  private Thread mNetworkThread;

  private final String mUserAgent;
  private final ExecutorService mExecutorService;
  private final CronetEngineBuilderImpl mBuilder;
  private final AtomicReference<Runnable> mInitializationCompleter =
      new AtomicReference<Runnable>();

  /**
   * Locks operations on the list of RequestFinishedInfo.Listeners, because operations can happen
   * on any thread. This should be used for fine-grained locking only. In particular, don't call
   * any UrlRequest methods that acquire mUrlRequestAdapterLock while holding this lock.
   */
  private final Object mFinishedListenerLock = new Object();
  @GuardedBy("mFinishedListenerLock")
  private final Map<RequestFinishedInfo.Listener, VersionSafeCallbacks.RequestFinishedInfoListener>
      mFinishedListenerMap = new HashMap<>();

  public CronetUrlRequestContext(CronetEngineBuilderImpl builder) {
    mBuilder = builder;
    // On android, all background threads (and all threads that are part
    // of background processes) are put in a cgroup that is allowed to
    // consume up to 5% of CPU - these worker threads spend the vast
    // majority of their time waiting on I/O, so making them contend with
    // background applications for a slice of CPU doesn't make much sense.
    // We want to hurry up and get idle.
    final int threadPriority =
        builder.threadPriority(THREAD_PRIORITY_BACKGROUND + THREAD_PRIORITY_MORE_FAVORABLE);
    mUserAgent = builder.getUserAgent();
    synchronized (mLock) {
      mEngine = new AndroidEngineBuilder(builder.getContext())
                    .addLogLevel(builder.getLogLevel())
                    .setOnEngineRunning(() -> {
                      mNetworkThread = Thread.currentThread();
                      mInitCompleted.open();
                      Runnable taskToExecuteWhenInitializationIsCompleted =
                          mInitializationCompleter.getAndSet(() -> {});
                      if (taskToExecuteWhenInitializationIsCompleted != null) {
                        taskToExecuteWhenInitializationIsCompleted.run();
                      }
                      return null;
                    })
                    .build();
    }
    mExecutorService =
        new ThreadPoolExecutor(2, 10, 50, TimeUnit.SECONDS, new LinkedBlockingQueue<>(),
                               r -> Executors.defaultThreadFactory().newThread(() -> {
                                 Thread.currentThread().setName("EnvoyCronetEngine");
                                 android.os.Process.setThreadPriority(threadPriority);
                                 r.run();
                               }));
  }

  public Engine getEnvoyEngine() {
    synchronized (mLock) {
      if (mEngine == null) {
        throw new IllegalStateException("Engine is shut down.");
      }
      return mEngine;
    }
  }

  CronetEngineBuilderImpl getBuilder() { return mBuilder; }

  void setTaskToExecuteWhenInitializationIsCompleted(Runnable runnable) {
    if (!mInitializationCompleter.compareAndSet(null, runnable)) {
      // The fact that the initializationCompleter was not null implies that the initialization
      // callback has already been executed. In this case, execute the task now - nothing else will
      // ever execute it otherwise.
      runnable.run();
    }
  }

  @Override
  public UrlRequestBase
  createRequest(String url, UrlRequest.Callback callback, Executor executor, int priority,
                Collection<Object> connectionAnnotations, boolean disableCache,
                boolean disableConnectionMigration, boolean allowDirectExecutor,
                boolean trafficStatsTagSet, int trafficStatsTag, boolean trafficStatsUidSet,
                int trafficStatsUid, RequestFinishedInfo.Listener requestFinishedListener,
                int idempotency) {
    return new CronetUrlRequest(this, callback, mExecutorService, executor, url, mUserAgent,
                                allowDirectExecutor, trafficStatsTagSet, trafficStatsTag,
                                trafficStatsUidSet, trafficStatsUid);
  }

  @Override
  protected ExperimentalBidirectionalStream
  createBidirectionalStream(String url, BidirectionalStream.Callback callback, Executor executor,
                            String httpMethod, List<Map.Entry<String, String>> requestHeaders,
                            @StreamPriority int priority,
                            boolean delayRequestHeadersUntilFirstFlush,
                            Collection<Object> connectionAnnotations, boolean trafficStatsTagSet,
                            int trafficStatsTag, boolean trafficStatsUidSet, int trafficStatsUid) {
    throw new UnsupportedOperationException("Can't create a bidi stream yet.");
  }

  @Override
  public ExperimentalBidirectionalStream.Builder
  newBidirectionalStreamBuilder(String url, BidirectionalStream.Callback callback,
                                Executor executor) {
    throw new UnsupportedOperationException("Can't create a bidi stream yet.");
  }

  @Override
  public String getVersionString() {
    return "CronetHttpURLConnection/" + ImplVersion.getCronetVersionWithLastChange();
  }

  @Override
  public void shutdown() {
    synchronized (mLock) {
      checkHaveAdapter();
      if (mActiveRequestCount.get() != 0) {
        throw new IllegalStateException("Cannot shutdown with active requests.");
      }
      // Destroying adapter stops the network thread, so it cannot be
      // called on network thread.
      if (Thread.currentThread() == mNetworkThread) {
        throw new IllegalThreadStateException("Cannot shutdown from network thread.");
      }
    }
    // Wait for init to complete on init and network thread (without lock,
    // so other thread could access it).
    mInitCompleted.block();

    // If not logging, this is a no-op.
    stopNetLog();

    synchronized (mLock) {
      // It is possible that adapter is already destroyed on another thread.
      if (!haveRequestContextAdapter()) {
        return;
      }
      mExecutorService.shutdown();
      mEngine.terminate();
      mEngine = null;
    }
  }

  @Override
  public void startNetLogToFile(String fileName, boolean logAll) {}

  @Override
  public void startNetLogToDisk(String dirPath, boolean logAll, int maxSize) {}

  @Override
  public void stopNetLog() {}

  @Override
  public byte[] getGlobalMetricsDeltas() {
    return new byte[0];
  }

  @Override
  public int getEffectiveConnectionType() {
    return EFFECTIVE_CONNECTION_TYPE_UNKNOWN;
  }

  @Override
  public int getHttpRttMs() {
    return CONNECTION_METRIC_UNKNOWN;
  }

  @Override
  public int getTransportRttMs() {
    return CONNECTION_METRIC_UNKNOWN;
  }

  @Override
  public int getDownstreamThroughputKbps() {
    return CONNECTION_METRIC_UNKNOWN;
  }

  @Override
  public void configureNetworkQualityEstimatorForTesting(boolean useLocalHostRequests,
                                                         boolean useSmallerResponses,
                                                         boolean disableOfflineCheck) {}

  @Override
  public void addRttListener(NetworkQualityRttListener listener) {}

  @Override
  public void removeRttListener(NetworkQualityRttListener listener) {}

  @Override
  public void addThroughputListener(NetworkQualityThroughputListener listener) {}

  @Override
  public void removeThroughputListener(NetworkQualityThroughputListener listener) {}

  @Override
  public void addRequestFinishedListener(RequestFinishedInfo.Listener listener) {
    synchronized (mFinishedListenerLock) {
      mFinishedListenerMap.put(listener,
                               new VersionSafeCallbacks.RequestFinishedInfoListener(listener));
    }
  }

  @Override
  public void removeRequestFinishedListener(RequestFinishedInfo.Listener listener) {
    synchronized (mFinishedListenerLock) { mFinishedListenerMap.remove(listener); }
  }

  boolean hasRequestFinishedListener() {
    synchronized (mFinishedListenerLock) { return !mFinishedListenerMap.isEmpty(); }
  }

  @Override
  public URLConnection openConnection(URL url) throws IOException {
    return openConnection(url, Proxy.NO_PROXY);
  }

  @Override
  public URLConnection openConnection(URL url, Proxy proxy) {
    if (proxy.type() != Proxy.Type.DIRECT) {
      throw new UnsupportedOperationException();
    }
    String protocol = url.getProtocol();
    if ("http".equals(protocol) || "https".equals(protocol)) {
      return new CronetHttpURLConnection(url, this);
    }
    throw new UnsupportedOperationException("Unexpected protocol:" + protocol);
  }

  @Override
  public URLStreamHandlerFactory createURLStreamHandlerFactory() {
    return new CronetURLStreamHandlerFactory(this);
  }

  /**
   * Mark request as started to prevent shutdown when there are active
   * requests.
   */
  void onRequestStarted() { mActiveRequestCount.incrementAndGet(); }

  /**
   * Mark request as finished to allow shutdown when there are no active
   * requests.
   */
  void onRequestDestroyed() { mActiveRequestCount.decrementAndGet(); }

  @GuardedBy("mLock")
  private void checkHaveAdapter() throws IllegalStateException {
    if (!haveRequestContextAdapter()) {
      throw new IllegalStateException("Engine is shut down.");
    }
  }

  @GuardedBy("mLock")
  private boolean haveRequestContextAdapter() {
    return mEngine != null;
  }

  void reportRequestFinished(final RequestFinishedInfo requestInfo) {
    List<RequestFinishedInfoListener> currentListeners;
    synchronized (mFinishedListenerLock) {
      if (mFinishedListenerMap.isEmpty()) {
        return;
      }
      currentListeners = new ArrayList<>(mFinishedListenerMap.values());
    }
    for (final VersionSafeCallbacks.RequestFinishedInfoListener listener : currentListeners) {
      Runnable task = new Runnable() {
        @Override
        public void run() {
          listener.onRequestFinished(requestInfo);
        }
      };
      postObservationTaskToExecutor(listener.getExecutor(), task);
    }
  }

  private static void postObservationTaskToExecutor(Executor executor, Runnable task) {
    try {
      executor.execute(task);
    } catch (RejectedExecutionException failException) {
      // TODO(carloseltuerto): use Envoy-Mobile logs - this is a hack.
      android.util.Log.e(CronetUrlRequestContext.LOG_TAG, "Exception posting task to executor",
                         failException);
    }
  }
}
