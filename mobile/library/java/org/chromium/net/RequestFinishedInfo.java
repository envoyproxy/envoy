package org.chromium.net;

import androidx.annotation.Nullable;
import java.util.Collection;
import java.util.Date;
import java.util.concurrent.Executor;

/**
 * Information about a finished request. Passed to {@link RequestFinishedInfo.Listener}.
 *
 * <p>To associate the data with the original request, use {@link
 * ExperimentalUrlRequest.Builder#addRequestAnnotation} or {@link
 * ExperimentalBidirectionalStream.Builder#addRequestAnnotation} to add a unique identifier when
 * creating the request, and call {@link #getAnnotations} when the {@link RequestFinishedInfo} is
 * received to retrieve the identifier.
 *
 * <p>{@hide} as it's a prototype.
 */
public abstract class RequestFinishedInfo {
  /**
   * Listens for finished requests for the purpose of collecting metrics.
   *
   * <p>{@hide} as it's a prototype.
   */
  public abstract static class Listener {
    private final Executor mExecutor;

    public Listener(Executor executor) {
      if (executor == null) {
        throw new IllegalStateException("Executor must not be null");
      }
      mExecutor = executor;
    }

    /**
     * Invoked with request info. Will be called in a task submitted to the {@link
     * java.util.concurrent.Executor} returned by {@link #getExecutor}.
     *
     * @param requestInfo {@link RequestFinishedInfo} for finished request.
     */
    public abstract void onRequestFinished(RequestFinishedInfo requestInfo);

    /**
     * Returns this listener's executor. Can be called on any thread.
     *
     * @return this listener's {@link java.util.concurrent.Executor}
     */
    public Executor getExecutor() { return mExecutor; }
  }

  /**
   * Metrics collected for a single request. Most of these metrics are timestamps for events during
   * the lifetime of the request, which can be used to build a detailed timeline for investigating
   * performance.
   *
   * <p>Events happen in this order:
   *
   * <ol>
   *   <li>{@link #getRequestStart request start}
   *   <li>{@link #getDnsStart DNS start}
   *   <li>{@link #getDnsEnd DNS end}
   *   <li>{@link #getConnectStart connect start}
   *   <li>{@link #getSslStart SSL start}
   *   <li>{@link #getSslEnd SSL end}
   *   <li>{@link #getConnectEnd connect end}
   *   <li>{@link #getSendingStart sending start}
   *   <li>{@link #getSendingEnd sending end}
   *   <li>{@link #getResponseStart response start}
   *   <li>{@link #getRequestEnd request end}
   * </ol>
   *
   * Start times are reported as the time when a request started blocking on event, not when the
   * event actually occurred, with the exception of push start and end. If a metric is not
   * meaningful or not available, including cases when a request finished before reaching that
   * stage, start and end times will be {@code null}. If no time was spent blocking on an event,
   * start and end will be the same time.
   *
   * <p>If the system clock is adjusted during the request, some of the {@link java.util.Date}
   * values might not match it. Timestamps are recorded using a clock that is guaranteed not to run
   * backwards. All timestamps are correct relative to the system clock at the time of request
   * start, and taking the difference between two timestamps will give the correct difference
   * between the events. In order to preserve this property, timestamps for events other than
   * request start are not guaranteed to match the system clock at the times they represent.
   *
   * <p>Most timing metrics are taken from <a
   * href="https://cs.chromium.org/chromium/src/net/base/load_timing_info.h">LoadTimingInfo</a>,
   * which holds the information for <a href="http://w3c.github.io/navigation-timing/"></a> and <a
   * href="https://www.w3.org/TR/resource-timing/"></a>.
   *
   * <p>{@hide} as it's a prototype.
   */
  public abstract static class Metrics {
    /**
     * Returns time when the request started.
     *
     * @return {@link java.util.Date} representing when the native request actually started. This
     *     timestamp will match the system clock at the time it represents.
     */
    @Nullable public abstract Date getRequestStart();

    /**
     * Returns time when DNS lookup started. This and {@link #getDnsEnd} will return non-null values
     * regardless of whether the result came from a DNS server or the local cache.
     *
     * @return {@link java.util.Date} representing when DNS lookup started. {@code null} if the
     *     socket was reused (see {@link #getSocketReused}).
     */
    @Nullable public abstract Date getDnsStart();

    /**
     * Returns time when DNS lookup finished. This and {@link #getDnsStart} will return non-null
     * values regardless of whether the result came from a DNS server or the local cache.
     *
     * @return {@link java.util.Date} representing when DNS lookup finished. {@code null} if the
     *     socket was reused (see {@link #getSocketReused}).
     */
    @Nullable public abstract Date getDnsEnd();

    /**
     * Returns time when connection establishment started.
     *
     * @return {@link java.util.Date} representing when connection establishment started, typically
     *     when DNS resolution finishes. {@code null} if the socket was reused (see {@link
     *     #getSocketReused}).
     */
    @Nullable public abstract Date getConnectStart();

    /**
     * Returns time when connection establishment finished.
     *
     * @return {@link java.util.Date} representing when connection establishment finished, after TCP
     *     connection is established and, if using HTTPS, SSL handshake is completed. For QUIC
     *     0-RTT, this represents the time of handshake confirmation and might happen later than
     *     {@link #getSendingStart}. {@code null} if the socket was reused (see {@link
     *     #getSocketReused}).
     */
    @Nullable public abstract Date getConnectEnd();

    /**
     * Returns time when SSL handshake started. For QUIC, this will be the same time as {@link
     * #getConnectStart}.
     *
     * @return {@link java.util.Date} representing when SSL handshake started. {@code null} if SSL
     *     is not used or if the socket was reused (see {@link #getSocketReused}).
     */
    @Nullable public abstract Date getSslStart();

    /**
     * Returns time when SSL handshake finished. For QUIC, this will be the same time as {@link
     * #getConnectEnd}.
     *
     * @return {@link java.util.Date} representing when SSL handshake finished. {@code null} if SSL
     *     is not used or if the socket was reused (see {@link #getSocketReused}).
     */
    @Nullable public abstract Date getSslEnd();

    /**
     * Returns time when sending the request started.
     *
     * @return {@link java.util.Date} representing when sending HTTP request headers started.
     */
    @Nullable public abstract Date getSendingStart();

    /**
     * Returns time when sending the request finished.
     *
     * @return {@link java.util.Date} representing when sending HTTP request body finished. (Sending
     *     request body happens after sending request headers.)
     */
    @Nullable public abstract Date getSendingEnd();

    /**
     * Returns time when first byte of HTTP/2 server push was received.
     *
     * @return {@link java.util.Date} representing when the first byte of an HTTP/2 server push was
     *     received. {@code null} if server push is not used.
     */
    @Nullable public abstract Date getPushStart();

    /**
     * Returns time when last byte of HTTP/2 server push was received.
     *
     * @return {@link java.util.Date} representing when the last byte of an HTTP/2 server push was
     *     received. {@code null} if server push is not used.
     */
    @Nullable public abstract Date getPushEnd();

    /**
     * Returns time when the end of the response headers was received.
     *
     * @return {@link java.util.Date} representing when the end of the response headers was
     *     received.
     */
    @Nullable public abstract Date getResponseStart();

    /**
     * Returns time when the request finished.
     *
     * @return {@link java.util.Date} representing when the request finished.
     */
    @Nullable public abstract Date getRequestEnd();

    /**
     * Returns whether the socket was reused from a previous request. In HTTP/2 or QUIC, if streams
     * are multiplexed in a single connection, returns {@code true} for all streams after the first.
     *
     * @return whether this request reused a socket from a previous request. When {@code true}, DNS,
     *     connection, and SSL times will be {@code null}.
     */
    public abstract boolean getSocketReused();

    /**
     * Returns milliseconds between request initiation and first byte of response headers, or {@code
     * null} if not collected. TODO(mgersh): Remove once new API works http://crbug.com/629194
     * {@hide}
     */
    @Nullable public abstract Long getTtfbMs();

    /**
     * Returns milliseconds between request initiation and finish, including a failure or
     * cancellation, or {@code null} if not collected. TODO(mgersh): Remove once new API works
     * http://crbug.com/629194 {@hide}
     */
    @Nullable public abstract Long getTotalTimeMs();

    /**
     * Returns total bytes sent over the network transport layer, or {@code null} if not collected.
     */
    @Nullable public abstract Long getSentByteCount();

    /**
     * Returns total bytes received over the network transport layer, or {@code null} if not
     * collected. Number of bytes does not include any previous redirects.
     */
    @Nullable public abstract Long getReceivedByteCount();
  }

  /**
   * Reason value indicating that the request succeeded. Returned from {@link #getFinishedReason}.
   */
  public static final int SUCCEEDED = 0;
  /**
   * Reason value indicating that the request failed or returned an error. Returned from {@link
   * #getFinishedReason}.
   */
  public static final int FAILED = 1;
  /**
   * Reason value indicating that the request was canceled. Returned from {@link
   * #getFinishedReason}.
   */
  public static final int CANCELED = 2;

  /**
   * Returns the request's original URL.
   *
   * @return the request's original URL
   */
  public abstract String getUrl();

  /**
   * Returns the objects that the caller has supplied when initiating the request, using {@link
   * ExperimentalUrlRequest.Builder#addRequestAnnotation} or {@link
   * ExperimentalBidirectionalStream.Builder#addRequestAnnotation}. Annotations can be used to
   * associate a {@link RequestFinishedInfo} with the original request or type of request.
   *
   * @return annotations supplied when creating the request
   */
  public abstract Collection<Object> getAnnotations();

  // TODO(klm): Collect and return a chain of Metrics objects for redirect responses.
  // TODO(mgersh): Update this javadoc when new metrics are fully implemented
  /**
   * Returns metrics collected for this request.
   *
   * <p>The reported times and bytes account for all redirects, i.e. the TTFB is from the start of
   * the original request to the ultimate response headers, the TTLB is from the start of the
   * original request to the end of the ultimate response, the received byte count is for all
   * redirects and the ultimate response combined. These cumulative metric definitions are
   * debatable, but are chosen to make sense for user-facing latency analysis.
   *
   * @return metrics collected for this request.
   */
  public abstract Metrics getMetrics();

  /**
   * Returns the reason why the request finished.
   *
   * @return one of {@link #SUCCEEDED}, {@link #FAILED}, or {@link #CANCELED}
   */
  public abstract int getFinishedReason();

  /**
   * Returns a {@link UrlResponseInfo} for the request, if its response had started.
   *
   * @return {@link UrlResponseInfo} for the request, if its response had started.
   */
  @Nullable public abstract UrlResponseInfo getResponseInfo();

  /**
   * If the request failed, returns the same {@link CronetException} provided to {@link
   * UrlRequest.Callback#onFailed}.
   *
   * @return the request's {@link CronetException}, if the request failed
   */
  @Nullable public abstract CronetException getException();
}
