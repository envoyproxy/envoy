package org.chromium.net.impl;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.atomic.AtomicLong;
import org.chromium.net.UrlResponseInfo;

/**
 * Implements the container for basic information about a response. Included in
 * {@link org.chromium.net.UrlRequest.Callback} callbacks. Each
 * {@link org.chromium.net.UrlRequest.Callback#onRedirectReceived onRedirectReceived()}
 * callback gets a different copy of {@code UrlResponseInfo} describing a particular
 * redirect response.
 */
public final class CronvoyUrlResponseInfoImpl extends UrlResponseInfo {
  private List<String> mResponseInfoUrlChain;
  private int mHttpStatusCode;
  private String mHttpStatusText;
  private boolean mWasCached;
  private String mNegotiatedProtocol;
  private String mProxyServer;
  private final AtomicLong mReceivedByteCount = new AtomicLong();
  private HeaderBlockImpl mHeaders;

  /**
   * Unmodifiable container of response headers or trailers.
   */
  public static final class HeaderBlockImpl extends HeaderBlock {
    private final List<Map.Entry<String, String>> mAllHeadersList;
    private Map<String, List<String>> mHeadersMap;

    HeaderBlockImpl(List<Map.Entry<String, String>> allHeadersList) {
      mAllHeadersList = allHeadersList;
    }

    @Override
    public List<Map.Entry<String, String>> getAsList() {
      return mAllHeadersList;
    }

    @Override
    public Map<String, List<String>> getAsMap() {
      // This is potentially racy...but races will only result in wasted resource.
      if (mHeadersMap != null) {
        return mHeadersMap;
      }
      Map<String, List<String>> map = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
      for (Map.Entry<String, String> entry : mAllHeadersList) {
        List<String> values = new ArrayList<String>();
        if (map.containsKey(entry.getKey())) {
          values.addAll(map.get(entry.getKey()));
        }
        values.add(entry.getValue());
        map.put(entry.getKey(), Collections.unmodifiableList(values));
      }
      mHeadersMap = Collections.unmodifiableMap(map);
      return mHeadersMap;
    }
  }

  /**
   * Creates an implementation of {@link UrlResponseInfo}.
   *
   * @param urlChain the URL chain. The first entry is the originally requested URL;
   *         the following entries are redirects followed.
   * @param httpStatusCode the HTTP status code.
   * @param httpStatusText the HTTP status text of the status line.
   * @param allHeadersList list of response header field and value pairs.
   * @param wasCached {@code true} if the response came from the cache, {@code false}
   *         otherwise.
   * @param negotiatedProtocol the protocol negotiated with the server.
   * @param proxyServer the proxy server that was used for the request.
   * @param receivedByteCount minimum count of bytes received from the network to process this
   *         request.
   */
  public CronvoyUrlResponseInfoImpl(List<String> urlChain, int httpStatusCode,
                                    String httpStatusText,
                                    List<Map.Entry<String, String>> allHeadersList,
                                    boolean wasCached, String negotiatedProtocol,
                                    String proxyServer, long receivedByteCount) {
    mResponseInfoUrlChain = Collections.unmodifiableList(urlChain);
    mHttpStatusCode = httpStatusCode;
    mHttpStatusText = httpStatusText;
    mHeaders = new HeaderBlockImpl(Collections.unmodifiableList(allHeadersList));
    mWasCached = wasCached;
    mNegotiatedProtocol = negotiatedProtocol;
    mProxyServer = proxyServer;
    mReceivedByteCount.set(receivedByteCount);
  }

  /**
   * Creates an empty implementation of {@link UrlResponseInfo}.
   */
  public CronvoyUrlResponseInfoImpl() {}

  /**
   * Sets response values.
   *
   * @param urlChain the URL chain. The first entry is the originally requested URL;
   *         the following entries are redirects followed.
   * @param httpStatusCode the HTTP status code.
   * @param httpStatusText the HTTP status text of the status line.
   * @param allHeadersList list of response header field and value pairs.
   * @param wasCached {@code true} if the response came from the cache, {@code false}
   *         otherwise.
   * @param negotiatedProtocol the protocol negotiated with the server.
   * @param proxyServer the proxy server that was used for the request.
   */
  public void setResponseValues(List<String> urlChain, int httpStatusCode, String httpStatusText,
                                List<Map.Entry<String, String>> allHeadersList, boolean wasCached,
                                String negotiatedProtocol, String proxyServer) {
    mResponseInfoUrlChain = Collections.unmodifiableList(urlChain);
    mHttpStatusCode = httpStatusCode;
    mHttpStatusText = httpStatusText;
    mHeaders = new HeaderBlockImpl(Collections.unmodifiableList(allHeadersList));
    mWasCached = wasCached;
    mNegotiatedProtocol = negotiatedProtocol;
    mProxyServer = proxyServer;
  }

  @Override
  public String getUrl() {
    return mResponseInfoUrlChain.get(mResponseInfoUrlChain.size() - 1);
  }

  @Override
  public List<String> getUrlChain() {
    return mResponseInfoUrlChain;
  }

  @Override
  public int getHttpStatusCode() {
    return mHttpStatusCode;
  }

  @Override
  public String getHttpStatusText() {
    return mHttpStatusText;
  }

  @Override
  public List<Map.Entry<String, String>> getAllHeadersAsList() {
    return mHeaders.getAsList();
  }

  @Override
  public Map<String, List<String>> getAllHeaders() {
    return mHeaders.getAsMap();
  }

  @Override
  public boolean wasCached() {
    return mWasCached;
  }

  @Override
  public String getNegotiatedProtocol() {
    return mNegotiatedProtocol;
  }

  @Override
  public String getProxyServer() {
    return mProxyServer;
  }

  @Override
  public long getReceivedByteCount() {
    return mReceivedByteCount.get();
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

  /**
   * Sets mReceivedByteCount. Must not be called after request completion or cancellation.
   */
  public void setReceivedByteCount(long currentReceivedByteCount) {
    mReceivedByteCount.set(currentReceivedByteCount);
  }
}
