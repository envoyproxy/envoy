package org.chromium.net.urlconnection;

import java.net.URLStreamHandler;
import java.net.URLStreamHandlerFactory;
import org.chromium.net.ExperimentalCronetEngine;

/**
 * An implementation of {@link URLStreamHandlerFactory} to handle HTTP and HTTPS
 * traffic. An instance of this class can be installed via
 * {@link java.net.URL#setURLStreamHandlerFactory} thus using Cronet by default for all requests
 * created via {@link java.net.URL#openConnection}.
 * <p>
 * Cronet does not use certain HTTP features provided via the system:
 * <ul>
 * <li>the HTTP cache installed via
 *     {@link android.net.http.HttpResponseCache#install}</li>
 * <li>the HTTP authentication method installed via
 *     {@link java.net.Authenticator#setDefault}</li>
 * <li>the HTTP cookie storage installed via {@link java.net.CookieHandler#setDefault}</li>
 * </ul>
 * <p>
 * While Cronet supports and encourages requests using the HTTPS protocol,
 * Cronet does not provide support for the
 * {@link javax.net.ssl.HttpsURLConnection} API. This lack of support also
 * includes not using certain HTTPS features provided via the system:
 * <ul>
 * <li>the HTTPS hostname verifier installed via {@link
 *     javax.net.ssl.HttpsURLConnection#setDefaultHostnameVerifier(javax.net.ssl.HostnameVerifier)
 *     HttpsURLConnection.setDefaultHostnameVerifier(javax.net.ssl.HostnameVerifier)}</li>
 * <li>the HTTPS socket factory installed via {@link
 *     javax.net.ssl.HttpsURLConnection#setDefaultSSLSocketFactory(javax.net.ssl.SSLSocketFactory)
 *     HttpsURLConnection.setDefaultSSLSocketFactory(javax.net.ssl.SSLSocketFactory)}</li>
 * </ul>
 *
 * {@hide}
 */
public final class CronetURLStreamHandlerFactory implements URLStreamHandlerFactory {
  private final ExperimentalCronetEngine mCronetEngine;

  /**
   * Creates a {@link CronetURLStreamHandlerFactory} to handle HTTP and HTTPS
   * traffic.
   * @param cronetEngine the {@link CronetEngine} to be used.
   * @throws NullPointerException if config is null.
   */
  public CronetURLStreamHandlerFactory(ExperimentalCronetEngine cronetEngine) {
    if (cronetEngine == null) {
      throw new NullPointerException("CronetEngine is null.");
    }
    mCronetEngine = cronetEngine;
  }

  /**
   * Returns a {@link CronetHttpURLStreamHandler} for HTTP and HTTPS, and
   * {@code null} for other protocols.
   */
  @Override
  public URLStreamHandler createURLStreamHandler(String protocol) {
    if ("http".equals(protocol) || "https".equals(protocol)) {
      return new CronetHttpURLStreamHandler(mCronetEngine);
    }
    return null;
  }
}
