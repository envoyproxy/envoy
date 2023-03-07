package org.chromium.net.impl;

import androidx.annotation.IntDef;
import androidx.annotation.Nullable;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.net.URL;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import org.chromium.net.BidirectionalStream;
import org.chromium.net.ExperimentalBidirectionalStream;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ExperimentalUrlRequest;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UrlRequest;

/**
 * Base class of {@link CronetUrlRequestContext} and {@link JavaCronetEngine} that contains
 * shared logic.
 */
abstract class CronetEngineBase extends ExperimentalCronetEngine {

  /**
   * Creates a {@link UrlRequest} object. All callbacks will be called on {@code executor}'s thread.
   * {@code executor} must not run tasks on the current thread to prevent blocking networking
   * operations and causing exceptions during shutdown.
   *
   * @param url {@link URL} for the request.
   * @param callback callback object that gets invoked on different events.
   * @param executor {@link Executor} on which all callbacks will be invoked.
   * @param priority priority of the request which should be one of the {@link
   *     UrlRequest.Builder#REQUEST_PRIORITY_IDLE REQUEST_PRIORITY_*} values.
   * @param requestAnnotations Objects to pass on to {@link
   *     org.chromium.net.RequestFinishedInfo.Listener}.
   * @param disableCache disables cache for the request. If context is not set up to use cache this
   *     param has no effect.
   * @param disableConnectionMigration disables connection migration for this request if it is
   *     enabled for the session.
   * @param allowDirectExecutor whether executors used by this request are permitted to execute
   *     submitted tasks inline.
   * @param trafficStatsTagSet {@code true} if {@code trafficStatsTag} represents a TrafficStats tag
   *     to apply to sockets used to perform this request.
   * @param trafficStatsTag TrafficStats tag to apply to sockets used to perform this request.
   * @param trafficStatsUidSet {@code true} if {@code trafficStatsUid} represents a UID to attribute
   *     traffic used to perform this request.
   * @param trafficStatsUid UID to attribute traffic used to perform this request.
   * @param requestFinishedListener callback to get invoked with metrics when request is finished.
   *     Set to {@code null} if not used.
   * @param idempotency idempotency of the request which should be one of the {@link
   *     ExperimentalUrlRequest.Builder#DEFAULT_IDEMPOTENCY IDEMPOTENT NOT_IDEMPOTENT} values.
   * @return new request.
   */
  abstract UrlRequestBase createRequest(
      String url, UrlRequest.Callback callback, Executor executor, @RequestPriority int priority,
      Collection<Object> requestAnnotations, boolean disableCache,
      boolean disableConnectionMigration, boolean allowDirectExecutor, boolean trafficStatsTagSet,
      int trafficStatsTag, boolean trafficStatsUidSet, int trafficStatsUid,
      @Nullable RequestFinishedInfo.Listener requestFinishedListener, @Idempotency int idempotency);

  /**
   * Creates a {@link BidirectionalStream} object. {@code callback} methods will be invoked on
   * {@code executor}. {@code executor} must not run tasks on the current thread to prevent blocking
   * networking operations and causing exceptions during shutdown.
   *
   * @param url the URL for the stream
   * @param callback the object whose methods get invoked upon different events
   * @param executor the {@link Executor} on which all callbacks will be called
   * @param httpMethod the HTTP method to use for the stream
   * @param requestHeaders the list of request headers
   * @param priority priority of the stream which should be one of the {@link
   *     BidirectionalStream.Builder#STREAM_PRIORITY_IDLE STREAM_PRIORITY_*} values.
   * @param delayRequestHeadersUntilFirstFlush whether to delay sending request headers until
   *     flush() is called, and try to combine them with the next data frame.
   * @param requestAnnotations Objects to pass on to {@link
   *     org.chromium.net.RequestFinishedInfo.Listener}.
   * @param trafficStatsTagSet {@code true} if {@code trafficStatsTag} represents a TrafficStats tag
   *     to apply to sockets used to perform this request.
   * @param trafficStatsTag TrafficStats tag to apply to sockets used to perform this request.
   * @param trafficStatsUidSet {@code true} if {@code trafficStatsUid} represents a UID to attribute
   *     traffic used to perform this request.
   * @param trafficStatsUid UID to attribute traffic used to perform this request.
   * @return a new stream.
   */
  abstract ExperimentalBidirectionalStream createBidirectionalStream(
      String url, BidirectionalStream.Callback callback, Executor executor, String httpMethod,
      List<Map.Entry<String, String>> requestHeaders, @StreamPriority int priority,
      boolean delayRequestHeadersUntilFirstFlush, Collection<Object> requestAnnotations,
      boolean trafficStatsTagSet, int trafficStatsTag, boolean trafficStatsUidSet,
      int trafficStatsUid);

  @Override
  public ExperimentalUrlRequest.Builder
  newUrlRequestBuilder(String url, UrlRequest.Callback callback, Executor executor) {
    return new UrlRequestBuilderImpl(url, callback, executor, this);
  }

  @IntDef({UrlRequest.Builder.REQUEST_PRIORITY_IDLE, UrlRequest.Builder.REQUEST_PRIORITY_LOWEST,
           UrlRequest.Builder.REQUEST_PRIORITY_LOW, UrlRequest.Builder.REQUEST_PRIORITY_MEDIUM,
           UrlRequest.Builder.REQUEST_PRIORITY_HIGHEST})
  @Retention(RetentionPolicy.SOURCE)
  public @interface RequestPriority {}

  @IntDef({
      BidirectionalStream.Builder.STREAM_PRIORITY_IDLE,
      BidirectionalStream.Builder.STREAM_PRIORITY_LOWEST,
      BidirectionalStream.Builder.STREAM_PRIORITY_LOW,
      BidirectionalStream.Builder.STREAM_PRIORITY_MEDIUM,
      BidirectionalStream.Builder.STREAM_PRIORITY_HIGHEST,
  })
  @Retention(RetentionPolicy.SOURCE)
  public @interface StreamPriority {}

  @IntDef({ExperimentalUrlRequest.Builder.DEFAULT_IDEMPOTENCY,
           ExperimentalUrlRequest.Builder.IDEMPOTENT,
           ExperimentalUrlRequest.Builder.NOT_IDEMPOTENT})
  @Retention(RetentionPolicy.SOURCE)
  public @interface Idempotency {}
}
