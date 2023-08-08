package org.chromium.net.impl;

import androidx.annotation.IntDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.concurrent.Executor;
import org.chromium.net.ExperimentalUrlRequest;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UrlRequest;

/**
 * Base class for classes that implement {@link UrlRequest} including experimental
 * features. {@link CronvoyUrlRequest} and {@link JavaUrlRequest} extends this class.
 */
abstract class CronvoyUrlRequestBase extends ExperimentalUrlRequest {
  /**
   * Sets the HTTP method verb to use for this request. Must be done before
   * request has started.
   *
   * <p>The default when this method is not called is "GET" if the request has
   * no body or "POST" if it does.
   *
   * @param method "GET", "HEAD", "DELETE", "POST" or "PUT".
   */
  protected abstract void setHttpMethod(String method);

  /**
   * Adds a request header. Must be done before request has started.
   *
   * @param header header name.
   * @param value header value.
   */
  protected abstract void addHeader(String header, String value);

  /**
   * Sets upload data provider. Must be done before request has started. May only be
   * invoked once per request. Switches method to "POST" if not explicitly
   * set. Starting the request will throw an exception if a Content-Type
   * header is not set.
   *
   * @param uploadDataProvider responsible for providing the upload data.
   * @param executor All {@code uploadDataProvider} methods will be invoked
   *     using this {@code Executor}. May optionally be the same
   *     {@code Executor} the request itself is using.
   */
  protected abstract void setUploadDataProvider(UploadDataProvider uploadDataProvider,
                                                Executor executor);

  /**
   * Possible URL Request statuses.
   */
  @IntDef({Status.INVALID, Status.IDLE, Status.WAITING_FOR_STALLED_SOCKET_POOL,
           Status.WAITING_FOR_AVAILABLE_SOCKET, Status.WAITING_FOR_DELEGATE,
           Status.WAITING_FOR_CACHE, Status.DOWNLOADING_PAC_FILE, Status.RESOLVING_PROXY_FOR_URL,
           Status.RESOLVING_HOST_IN_PAC_FILE, Status.ESTABLISHING_PROXY_TUNNEL,
           Status.RESOLVING_HOST, Status.CONNECTING, Status.SSL_HANDSHAKE, Status.SENDING_REQUEST,
           Status.WAITING_FOR_RESPONSE, Status.READING_RESPONSE})
  @Retention(RetentionPolicy.SOURCE)
  @interface StatusValues {}
}
