package org.chromium.net;

import java.nio.ByteBuffer;
import java.util.concurrent.Executor;

/**
 * Controls an HTTP request (GET, PUT, POST etc). Created by {@link UrlRequest.Builder}, which can
 * be obtained by calling {@link CronetEngine#newUrlRequestBuilder}. Note: All methods must be
 * called on the {@link Executor} passed to {@link CronetEngine#newUrlRequestBuilder}.
 */
public abstract class UrlRequest {
  /**
   * Builder for {@link UrlRequest}s. Allows configuring requests before constructing them with
   * {@link Builder#build}. The builder can be created by calling {@link
   * CronetEngine#newUrlRequestBuilder}.
   */
  public abstract static class Builder {
    /**
     * Sets the HTTP method verb to use for this request.
     *
     * <p>The default when this method is not called is "GET" if the request has no body or "POST"
     * if it does.
     *
     * @param method "GET", "HEAD", "DELETE", "POST" or "PUT".
     * @return the builder to facilitate chaining.
     */
    public abstract Builder setHttpMethod(String method);

    /**
     * Adds a request header.
     *
     * @param header header name.
     * @param value header value.
     * @return the builder to facilitate chaining.
     */
    public abstract Builder addHeader(String header, String value);

    /**
     * Disables cache for the request. If context is not set up to use cache, this call has no
     * effect.
     *
     * @return the builder to facilitate chaining.
     */
    public abstract Builder disableCache();

    /** Lowest request priority. Passed to {@link #setPriority}. */
    public static final int REQUEST_PRIORITY_IDLE = 0;
    /** Very low request priority. Passed to {@link #setPriority}. */
    public static final int REQUEST_PRIORITY_LOWEST = 1;
    /** Low request priority. Passed to {@link #setPriority}. */
    public static final int REQUEST_PRIORITY_LOW = 2;
    /**
     * Medium request priority. Passed to {@link #setPriority}. This is the default priority given
     * to the request.
     */
    public static final int REQUEST_PRIORITY_MEDIUM = 3;
    /** Highest request priority. Passed to {@link #setPriority}. */
    public static final int REQUEST_PRIORITY_HIGHEST = 4;

    /**
     * Sets priority of the request which should be one of the {@link #REQUEST_PRIORITY_IDLE
     * REQUEST_PRIORITY_*} values. The request is given {@link #REQUEST_PRIORITY_MEDIUM} priority if
     * this method is not called.
     *
     * @param priority priority of the request which should be one of the {@link
     *     #REQUEST_PRIORITY_IDLE REQUEST_PRIORITY_*} values.
     * @return the builder to facilitate chaining.
     */
    public abstract Builder setPriority(int priority);

    /**
     * Sets upload data provider. Switches method to "POST" if not explicitly set. Starting the
     * request will throw an exception if a Content-Type header is not set.
     *
     * @param uploadDataProvider responsible for providing the upload data.
     * @param executor All {@code uploadDataProvider} methods will be invoked using this {@code
     *     Executor}. May optionally be the same {@code Executor} the request itself is using.
     * @return the builder to facilitate chaining.
     */
    public abstract Builder setUploadDataProvider(UploadDataProvider uploadDataProvider,
                                                  Executor executor);

    /**
     * Marks that the executors this request will use to notify callbacks (for {@code
     * UploadDataProvider}s and {@code UrlRequest.Callback}s) is intentionally performing inline
     * execution, like Guava's directExecutor or {@link
     * java.util.concurrent.ThreadPoolExecutor.CallerRunsPolicy}.
     *
     * <p><b>Warning:</b> This option makes it easy to accidentally block the network thread. It
     * should not be used if your callbacks perform disk I/O, acquire locks, or call into other code
     * you don't carefully control and audit.
     */
    public abstract Builder allowDirectExecutor();

    /**
     * Creates a {@link UrlRequest} using configuration within this {@link Builder}. The returned
     * {@code UrlRequest} can then be started by calling {@link UrlRequest#start}.
     *
     * @return constructed {@link UrlRequest} using configuration within this {@link Builder}.
     */
    public abstract UrlRequest build();
  }

  /**
   * Users of Cronet extend this class to receive callbacks indicating the progress of a {@link
   * UrlRequest} being processed. An instance of this class is passed in to {@link
   * UrlRequest.Builder}'s constructor when constructing the {@code UrlRequest}.
   *
   * <p>Note: All methods will be invoked on the thread of the {@link java.util.concurrent.Executor}
   * used during construction of the {@code UrlRequest}.
   */
  public abstract static class Callback {
    /**
     * Invoked whenever a redirect is encountered. This will only be invoked between the call to
     * {@link UrlRequest#start} and {@link Callback#onResponseStarted onResponseStarted()}. The body
     * of the redirect response, if it has one, will be ignored.
     *
     * <p>The redirect will not be followed until the URLRequest's {@link UrlRequest#followRedirect}
     * method is called, either synchronously or asynchronously.
     *
     * @param request Request being redirected.
     * @param info Response information.
     * @param newLocationUrl Location where request is redirected.
     * @throws Exception if an error occurs while processing a redirect. {@link #onFailed} will be
     *     called with the thrown exception set as the cause of the {@link CallbackException}.
     */
    public abstract void onRedirectReceived(UrlRequest request, UrlResponseInfo info,
                                            String newLocationUrl) throws Exception;

    /**
     * Invoked when the final set of headers, after all redirects, is received. Will only be invoked
     * once for each request.
     *
     * <p>With the exception of {@link Callback#onCanceled onCanceled()}, no other {@link Callback}
     * method will be invoked for the request, including {@link Callback#onSucceeded onSucceeded()}
     * and {@link Callback#onFailed onFailed()}, until {@link UrlRequest#read UrlRequest.read()} is
     * called to attempt to start reading the response body.
     *
     * @param request Request that started to get response.
     * @param info Response information.
     * @throws Exception if an error occurs while processing response start. {@link #onFailed} will
     *     be called with the thrown exception set as the cause of the {@link CallbackException}.
     */
    public abstract void onResponseStarted(UrlRequest request, UrlResponseInfo info)
        throws Exception;

    /**
     * Invoked whenever part of the response body has been read. Only part of the buffer may be
     * populated, even if the entire response body has not yet been consumed.
     *
     * <p>With the exception of {@link Callback#onCanceled onCanceled()}, no other {@link Callback}
     * method will be invoked for the request, including {@link Callback#onSucceeded onSucceeded()}
     * and {@link Callback#onFailed onFailed()}, until {@link UrlRequest#read UrlRequest.read()} is
     * called to attempt to continue reading the response body.
     *
     * @param request Request that received data.
     * @param info Response information.
     * @param byteBuffer The buffer that was passed in to {@link UrlRequest#read UrlRequest.read()},
     *     now containing the received data. The buffer's position is updated to the end of the
     *     received data. The buffer's limit is not changed.
     * @throws Exception if an error occurs while processing a read completion. {@link #onFailed}
     *     will be called with the thrown exception set as the cause of the {@link
     *     CallbackException}.
     */
    public abstract void onReadCompleted(UrlRequest request, UrlResponseInfo info,
                                         ByteBuffer byteBuffer) throws Exception;

    /**
     * Invoked when request is completed successfully. Once invoked, no other {@link Callback}
     * methods will be invoked.
     *
     * @param request Request that succeeded.
     * @param info Response information.
     */
    public abstract void onSucceeded(UrlRequest request, UrlResponseInfo info);

    /**
     * Invoked if request failed for any reason after {@link UrlRequest#start}. Once invoked, no
     * other {@link Callback} methods will be invoked. {@code error} provides information about the
     * failure.
     *
     * @param request Request that failed.
     * @param info Response information. May be {@code null} if no response was received.
     * @param error information about error.
     */
    public abstract void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error);

    /**
     * Invoked if request was canceled via {@link UrlRequest#cancel}. Once invoked, no other {@link
     * Callback} methods will be invoked. Default implementation takes no action.
     *
     * @param request Request that was canceled.
     * @param info Response information. May be {@code null} if no response was received.
     */
    public void onCanceled(UrlRequest request, UrlResponseInfo info) {}
  }

  /** Request status values returned by {@link #getStatus}. */
  public static class Status {

    /** This state indicates that the request is completed, canceled, or is not started. */
    public static final int INVALID = -1;
    /**
     * This state corresponds to a resource load that has either not yet begun or is idle waiting
     * for the consumer to do something to move things along (e.g. when the consumer of a {@link
     * UrlRequest} has not called {@link UrlRequest#read read()} yet).
     */
    public static final int IDLE = 0;
    /**
     * When a socket pool group is below the maximum number of sockets allowed per group, but a new
     * socket cannot be created due to the per-pool socket limit, this state is returned by all
     * requests for the group waiting on an idle connection, except those that may be serviced by a
     * pending new connection.
     */
    public static final int WAITING_FOR_STALLED_SOCKET_POOL = 1;
    /**
     * When a socket pool group has reached the maximum number of sockets allowed per group, this
     * state is returned for all requests that don't have a socket, except those that correspond to
     * a pending new connection.
     */
    public static final int WAITING_FOR_AVAILABLE_SOCKET = 2;
    /**
     * This state indicates that the URLRequest delegate has chosen to block this request before it
     * was sent over the network.
     */
    public static final int WAITING_FOR_DELEGATE = 3;
    /**
     * This state corresponds to a resource load that is blocked waiting for access to a resource in
     * the cache. If multiple requests are made for the same resource, the first request will be
     * responsible for writing (or updating) the cache entry and the second request will be deferred
     * until the first completes. This may be done to optimize for cache reuse.
     */
    public static final int WAITING_FOR_CACHE = 4;
    /**
     * This state corresponds to a resource being blocked waiting for the PAC script to be
     * downloaded.
     */
    public static final int DOWNLOADING_PAC_FILE = 5;
    /**
     * This state corresponds to a resource load that is blocked waiting for a proxy autoconfig
     * script to return a proxy server to use.
     */
    public static final int RESOLVING_PROXY_FOR_URL = 6;
    /**
     * This state corresponds to a resource load that is blocked waiting for a proxy autoconfig
     * script to return a proxy server to use, but that proxy script is busy resolving the IP
     * address of a host.
     */
    public static final int RESOLVING_HOST_IN_PAC_FILE = 7;
    /**
     * This state indicates that we're in the process of establishing a tunnel through the proxy
     * server.
     */
    public static final int ESTABLISHING_PROXY_TUNNEL = 8;
    /**
     * This state corresponds to a resource load that is blocked waiting for a host name to be
     * resolved. This could either indicate resolution of the origin server corresponding to the
     * resource or to the host name of a proxy server used to fetch the resource.
     */
    public static final int RESOLVING_HOST = 9;
    /**
     * This state corresponds to a resource load that is blocked waiting for a TCP connection (or
     * other network connection) to be established. HTTP requests that reuse a keep-alive connection
     * skip this state.
     */
    public static final int CONNECTING = 10;
    /**
     * This state corresponds to a resource load that is blocked waiting for the SSL handshake to
     * complete.
     */
    public static final int SSL_HANDSHAKE = 11;
    /**
     * This state corresponds to a resource load that is blocked waiting to completely upload a
     * request to a server. In the case of a HTTP POST request, this state includes the period of
     * time during which the message body is being uploaded.
     */
    public static final int SENDING_REQUEST = 12;
    /**
     * This state corresponds to a resource load that is blocked waiting for the response to a
     * network request. In the case of a HTTP transaction, this corresponds to the period after the
     * request is sent and before all of the response headers have been received.
     */
    public static final int WAITING_FOR_RESPONSE = 13;
    /**
     * This state corresponds to a resource load that is blocked waiting for a read to complete. In
     * the case of a HTTP transaction, this corresponds to the period after the response headers
     * have been received and before all of the response body has been downloaded. (NOTE: This state
     * only applies for an {@link UrlRequest} while there is an outstanding {@link UrlRequest#read
     * read()} operation.)
     */
    public static final int READING_RESPONSE = 14;

    private Status() {}
  }

  /** Listener class used with {@link #getStatus} to receive the status of a {@link UrlRequest}. */
  public abstract static class StatusListener {
    /**
     * Invoked on {@link UrlRequest}'s {@link Executor}'s thread when request status is obtained.
     *
     * @param status integer representing the status of the request. It is one of the values defined
     *     in {@link Status}.
     */
    public abstract void onStatus(int status);
  }

  /**
   * Starts the request, all callbacks go to {@link Callback}. May only be called once. May not be
   * called if {@link #cancel} has been called.
   */
  public abstract void start();

  /**
   * Follows a pending redirect. Must only be called at most once for each invocation of {@link
   * Callback#onRedirectReceived onRedirectReceived()}.
   */
  public abstract void followRedirect();

  /**
   * Attempts to read part of the response body into the provided buffer. Must only be called at
   * most once in response to each invocation of the {@link Callback#onResponseStarted
   * onResponseStarted()} and {@link Callback#onReadCompleted onReadCompleted()} methods of the
   * {@link Callback}. Each call will result in an asynchronous call to either the {@link Callback
   * Callback's} {@link Callback#onReadCompleted onReadCompleted()} method if data is read, its
   * {@link Callback#onSucceeded onSucceeded()} method if there's no more data to read, or its
   * {@link Callback#onFailed onFailed()} method if there's an error.
   *
   * @param buffer {@link ByteBuffer} to write response body to. Must be a direct ByteBuffer. The
   *     embedder must not read or modify buffer's position, limit, or data between its position and
   *     limit until the request calls back into the {@link Callback}.
   */
  public abstract void read(ByteBuffer buffer);

  /**
   * Cancels the request. Can be called at any time. {@link Callback#onCanceled onCanceled()} will
   * be invoked when cancellation is complete and no further callback methods will be invoked. If
   * the request has completed or has not started, calling {@code cancel()} has no effect and {@code
   * onCanceled()} will not be invoked. If the {@link Executor} passed in during {@code UrlRequest}
   * construction runs tasks on a single thread, and {@code cancel()} is called on that thread, no
   * callback methods (besides {@code onCanceled()}) will be invoked after {@code cancel()} is
   * called. Otherwise, at most one callback method may be invoked after {@code cancel()} has
   * completed.
   */
  public abstract void cancel();

  /**
   * Returns {@code true} if the request was successfully started and is now finished (completed,
   * canceled, or failed).
   *
   * @return {@code true} if the request was successfully started and is now finished (completed,
   *     canceled, or failed).
   */
  public abstract boolean isDone();

  /**
   * Queries the status of the request.
   *
   * @param listener a {@link StatusListener} that will be invoked with the request's current
   *     status. {@code listener} will be invoked back on the {@link Executor} passed in when the
   *     request was created.
   */
  public abstract void getStatus(final StatusListener listener);

  // Note:  There are deliberately no accessors for the results of the request
  // here. Having none removes any ambiguity over when they are populated,
  // particularly in the redirect case.
}
