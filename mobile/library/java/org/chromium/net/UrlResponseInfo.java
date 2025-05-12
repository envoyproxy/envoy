package org.chromium.net;

import java.util.List;
import java.util.Map;

/**
 * Basic information about a response. Included in {@link UrlRequest.Callback} callbacks. Each
 * {@link UrlRequest.Callback#onRedirectReceived onRedirectReceived()} callback gets a different
 * copy of {@code UrlResponseInfo} describing a particular redirect response.
 */
public abstract class UrlResponseInfo {
  /** Unmodifiable container of response headers or trailers. {@hide}. */
  public abstract static class HeaderBlock {
    /**
     * Returns an unmodifiable list of the response header field and value pairs. The headers are in
     * the same order they are received over the wire.
     *
     * @return an unmodifiable list of response header field and value pairs
     */
    public abstract List<Map.Entry<String, String>> getAsList();

    /**
     * Returns an unmodifiable map from response-header field names to lists of values. Each list of
     * values for a single header field is in the same order they were received over the wire.
     *
     * @return an unmodifiable map from response-header field names to lists of values
     */
    public abstract Map<String, List<String>> getAsMap();
  }

  /**
   * Returns the URL the response is for. This is the URL after following redirects, so it may not
   * be the originally requested URL.
   *
   * @return the URL the response is for.
   */
  public abstract String getUrl();

  /**
   * Returns the URL chain. The first entry is the originally requested URL; the following entries
   * are redirects followed.
   *
   * @return the URL chain.
   */
  public abstract List<String> getUrlChain();

  /**
   * Returns the HTTP status code. When a resource is retrieved from the cache, whether it was
   * revalidated or not, the original status code is returned.
   *
   * @return the HTTP status code.
   */
  public abstract int getHttpStatusCode();

  /**
   * Returns the HTTP status text of the status line. For example, if the request received a
   * "HTTP/1.1 200 OK" response, this method returns "OK".
   *
   * @return the HTTP status text of the status line.
   */
  public abstract String getHttpStatusText();

  /**
   * Returns an unmodifiable list of response header field and value pairs. The headers are in the
   * same order they are received over the wire.
   *
   * @return an unmodifiable list of response header field and value pairs.
   */
  public abstract List<Map.Entry<String, String>> getAllHeadersAsList();

  /**
   * Returns an unmodifiable map of the response-header fields and values. Each list of values for a
   * single header field is in the same order they were received over the wire.
   *
   * @return an unmodifiable map of the response-header fields and values.
   */
  public abstract Map<String, List<String>> getAllHeaders();

  /**
   * Returns {@code true} if the response came from the cache, including requests that were
   * revalidated over the network before being retrieved from the cache.
   *
   * @return {@code true} if the response came from the cache, {@code false} otherwise.
   */
  public abstract boolean wasCached();

  /**
   * Returns the protocol (for example 'quic/1+spdy/3') negotiated with the server. Returns an empty
   * string if no protocol was negotiated, the protocol is not known, or when using plain HTTP or
   * HTTPS.
   *
   * @return the protocol negotiated with the server.
   */
  // TODO(mef): Figure out what this returns in the cached case, both with
  // and without a revalidation request.
  public abstract String getNegotiatedProtocol();

  /**
   * Returns the proxy server that was used for the request.
   *
   * @return the proxy server that was used for the request.
   */
  public abstract String getProxyServer();

  /**
   * Returns a minimum count of bytes received from the network to process this request. This count
   * may ignore certain overheads (for example IP and TCP/UDP framing, SSL handshake and framing,
   * proxy handling). This count is taken prior to decompression (for example GZIP) and includes
   * headers and data from all redirects.
   *
   * <p>This value may change (even for one {@link UrlResponseInfo} instance) as the request
   * progresses until completion, when {@link UrlRequest.Callback#onSucceeded onSucceeded()}, {@link
   * UrlRequest.Callback#onFailed onFailed()}, or {@link UrlRequest.Callback#onCanceled
   * onCanceled()} is called.
   *
   * @return a minimum count of bytes received from the network to process this request.
   */
  public abstract long getReceivedByteCount();
}
