package org.cronvoy;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.atomic.AtomicLong;
import org.chromium.net.UrlResponseInfo;

/**
 * Implements the container for basic information about a response. Included in {@link
 * org.chromium.net.UrlRequest.Callback} callbacks. Each {@link
 * org.chromium.net.UrlRequest.Callback#onRedirectReceived onRedirectReceived()} callback gets a
 * different copy of {@code UrlResponseInfo} describing a particular redirect response.
 */
final class UrlResponseInfoImpl extends UrlResponseInfo {

  private final List<String> responseInfoUrlChain;
  private final int httpStatusCode;
  private final String httpStatusText;
  private final boolean wasCached;
  private final String negotiatedProtocol;
  private final String proxyServer;
  private final AtomicLong receivedByteCount;
  private final HeaderBlockImpl headers;

  /** Unmodifiable container of response headers or trailers. */
  static final class HeaderBlockImpl extends HeaderBlock {
    private final List<Map.Entry<String, String>> allHeadersList;
    private Map<String, List<String>> headersMap;

    HeaderBlockImpl(List<Map.Entry<String, String>> allHeadersList) {
      this.allHeadersList = allHeadersList;
    }

    @Override
    public List<Map.Entry<String, String>> getAsList() {
      return allHeadersList;
    }

    @Override
    public Map<String, List<String>> getAsMap() {
      // This is potentially racy...but races will only result in wasted resource.
      if (headersMap != null) {
        return headersMap;
      }
      Map<String, List<String>> map = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
      for (Map.Entry<String, String> entry : allHeadersList) {
        List<String> values = new ArrayList<String>();
        if (map.containsKey(entry.getKey())) {
          values.addAll(map.get(entry.getKey()));
        }
        values.add(entry.getValue());
        map.put(entry.getKey(), Collections.unmodifiableList(values));
      }
      headersMap = Collections.unmodifiableMap(map);
      return headersMap;
    }
  }

  /**
   * Creates an implementation of {@link UrlResponseInfo}.
   *
   * @param urlChain the URL chain. The first entry is the originally requested URL; the following
   *     entries are redirects followed.
   * @param httpStatusCode the HTTP status code.
   * @param httpStatusText the HTTP status text of the status line.
   * @param allHeadersList list of response header field and value pairs.
   * @param wasCached {@code true} if the response came from the cache, {@code false} otherwise.
   * @param negotiatedProtocol the protocol negotiated with the server.
   * @param proxyServer the proxy server that was used for the request.
   * @param receivedByteCount minimum count of bytes received from the network to process this
   *     request.
   */
  UrlResponseInfoImpl(List<String> urlChain, int httpStatusCode, String httpStatusText,
                      List<Map.Entry<String, String>> allHeadersList, boolean wasCached,
                      String negotiatedProtocol, String proxyServer, long receivedByteCount) {
    responseInfoUrlChain = Collections.unmodifiableList(urlChain);
    this.httpStatusCode = httpStatusCode;
    this.httpStatusText = httpStatusText;
    headers = new HeaderBlockImpl(Collections.unmodifiableList(allHeadersList));
    this.wasCached = wasCached;
    this.negotiatedProtocol = negotiatedProtocol;
    this.proxyServer = proxyServer;
    this.receivedByteCount = new AtomicLong(receivedByteCount);
  }

  @Override
  public String getUrl() {
    return responseInfoUrlChain.get(responseInfoUrlChain.size() - 1);
  }

  @Override
  public List<String> getUrlChain() {
    return responseInfoUrlChain;
  }

  @Override
  public int getHttpStatusCode() {
    return httpStatusCode;
  }

  @Override
  public String getHttpStatusText() {
    return httpStatusText;
  }

  @Override
  public List<Map.Entry<String, String>> getAllHeadersAsList() {
    return headers.getAsList();
  }

  @Override
  public Map<String, List<String>> getAllHeaders() {
    return headers.getAsMap();
  }

  @Override
  public boolean wasCached() {
    return wasCached;
  }

  @Override
  public String getNegotiatedProtocol() {
    return negotiatedProtocol;
  }

  @Override
  public String getProxyServer() {
    return proxyServer;
  }

  @Override
  public long getReceivedByteCount() {
    return receivedByteCount.get();
  }

  @Override
  public String toString() {
    return String.format(Locale.ROOT,
                         "UrlResponseInfo@[%s][%s]: urlChain = %s, "
                             + "httpStatus = %d %s, headers = %s, wasCached = %b, "
                             + "negotiatedProtocol = %s, proxyServer= %s, receivedByteCount = %d",
                         // Prevent asserting on the contents of this string
                         Integer.toHexString(System.identityHashCode(this)), getUrl(),
                         getUrlChain().toString(), getHttpStatusCode(), getHttpStatusText(),
                         getAllHeadersAsList().toString(), wasCached(), getNegotiatedProtocol(),
                         getProxyServer(), getReceivedByteCount());
  }

  /** Sets receivedByteCount. Must not be called after request completion or cancellation. */
  void setReceivedByteCount(long currentReceivedByteCount) {
    receivedByteCount.set(currentReceivedByteCount);
  }
}
