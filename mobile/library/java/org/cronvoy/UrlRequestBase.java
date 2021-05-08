package org.cronvoy;

import java.util.concurrent.Executor;
import org.chromium.net.ExperimentalUrlRequest;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UrlRequest;

/**
 * Base class for classes that implement {@link UrlRequest} including experimental features. {@link
 * CronvoyUrlRequest} extends this class.
 */
abstract class UrlRequestBase extends ExperimentalUrlRequest {

  /**
   * Sets the HTTP method verb to use for this request. Must be done before request has started.
   *
   * <p>The default when this method is not called is "GET" if the request has no body or "POST" if
   * it does.
   *
   * @param method "GET", "HEAD", "DELETE", "POST" or "PUT".
   */
  abstract void setHttpMethod(String method);
  /**
   * Adds a request header. Must be done before request has started.
   *
   * @param header header name.
   * @param value header value.
   */
  abstract void addHeader(String header, String value);
  /**
   * Sets upload data provider. Must be done before request has started. May only be invoked once
   * per request. Switches method to "POST" if not explicitly set. Starting the request will throw
   * an exception if a Content-Type header is not set.
   *
   * @param uploadDataProvider responsible for providing the upload data.
   * @param executor All {@code uploadDataProvider} methods will be invoked using this {@code
   *     Executor}. May optionally be the same {@code Executor} the request itself is using.
   */
  abstract void setUploadDataProvider(UploadDataProvider uploadDataProvider, Executor executor);
}
