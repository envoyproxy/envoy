package org.chromium.net;

import android.content.Context;
import android.net.http.HttpResponseCache;
import android.util.Log;
import androidx.annotation.VisibleForTesting;
import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandlerFactory;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.concurrent.Executor;
import javax.net.ssl.HttpsURLConnection;

/**
 * An engine to process {@link UrlRequest}s, which uses the best HTTP stack available on the current
 * platform. An instance of this class can be created using {@link Builder}.
 */
public abstract class CronetEngine {
  private static final String TAG = CronetEngine.class.getSimpleName();

  /**
   * A builder for {@link CronetEngine}s, which allows runtime configuration of {@code
   * CronetEngine}. Configuration options are set on the builder and then {@link #build} is called
   * to create the {@code CronetEngine}.
   */
  // NOTE(kapishnikov): In order to avoid breaking the existing API clients, all future methods
  // added to this class and other API classes must have default implementation.
  public static class Builder {
    /**
     * A class which provides a method for loading the cronet native library. Apps needing to
     * implement custom library loading logic can inherit from this class and pass an instance to
     * {@link CronetEngine.Builder#setLibraryLoader}. For example, this might be required to work
     * around {@code UnsatisfiedLinkError}s caused by flaky installation on certain older devices.
     */
    public abstract static class LibraryLoader {
      /**
       * Loads the native library.
       *
       * @param libName name of the library to load
       */
      public abstract void loadLibrary(String libName);
    }

    /** Reference to the actual builder implementation. {@hide exclude from JavaDoc}. */
    protected final ICronetEngineBuilder mBuilderDelegate;

    /**
     * Constructs a {@link Builder} object that facilitates creating a {@link CronetEngine}. The
     * default configuration enables HTTP/2 and QUIC, but disables the HTTP cache.
     *
     * @param context Android {@link Context}, which is used by {@link Builder} to retrieve the
     *     application context. A reference to only the application context will be kept, so as to
     *     avoid extending the lifetime of {@code context} unnecessarily.
     */
    public Builder(Context context) { this(createBuilderDelegate(context)); }

    /**
     * Constructs {@link Builder} with a given delegate that provides the actual implementation of
     * the {@code Builder} methods. This constructor is used only by the internal implementation.
     *
     * @param builderDelegate delegate that provides the actual implementation.
     *     <p>{@hide}
     */
    public Builder(ICronetEngineBuilder builderDelegate) { mBuilderDelegate = builderDelegate; }

    /**
     * Constructs a User-Agent string including application name and version, system build version,
     * model and id, and Cronet version.
     *
     * @return User-Agent string.
     */
    public String getDefaultUserAgent() { return mBuilderDelegate.getDefaultUserAgent(); }

    /**
     * Overrides the User-Agent header for all requests. An explicitly set User-Agent header (set
     * using {@link UrlRequest.Builder#addHeader}) will override a value set using this function.
     *
     * @param userAgent the User-Agent string to use for all requests.
     * @return the builder to facilitate chaining.
     */
    public Builder setUserAgent(String userAgent) {
      mBuilderDelegate.setUserAgent(userAgent);
      return this;
    }

    /**
     * Sets directory for HTTP Cache and Cookie Storage. The directory must exist.
     *
     * <p><b>NOTE:</b> Do not use the same storage directory with more than one {@code CronetEngine}
     * at a time. Access to the storage directory does not support concurrent access by multiple
     * {@code CronetEngine}s.
     *
     * @param value path to existing directory.
     * @return the builder to facilitate chaining.
     */
    public Builder setStoragePath(String value) {
      mBuilderDelegate.setStoragePath(value);
      return this;
    }

    /**
     * Sets a {@link LibraryLoader} to be used to load the native library. If not set, the library
     * will be loaded using {@link System#loadLibrary}.
     *
     * @param loader {@code LibraryLoader} to be used to load the native library.
     * @return the builder to facilitate chaining.
     */
    public Builder setLibraryLoader(LibraryLoader loader) {
      mBuilderDelegate.setLibraryLoader(loader);
      return this;
    }

    /**
     * Sets whether <a href="https://www.chromium.org/quic">QUIC</a> protocol is enabled. Defaults
     * to enabled. If QUIC is enabled, then QUIC User Agent Id containing application name and
     * Cronet version is sent to the server.
     *
     * @param value {@code true} to enable QUIC, {@code false} to disable.
     * @return the builder to facilitate chaining.
     */
    public Builder enableQuic(boolean value) {
      mBuilderDelegate.enableQuic(value);
      return this;
    }

    /**
     * Sets whether <a href="https://tools.ietf.org/html/rfc7540">HTTP/2</a> protocol is enabled.
     * Defaults to enabled.
     *
     * @param value {@code true} to enable HTTP/2, {@code false} to disable.
     * @return the builder to facilitate chaining.
     */
    public Builder enableHttp2(boolean value) {
      mBuilderDelegate.enableHttp2(value);
      return this;
    }

    /**
     * @deprecated SDCH is deprecated in Cronet M63. This method is a no-op. {@hide exclude from
     *     JavaDoc}.
     */
    @Deprecated
    public Builder enableSdch(boolean value) {
      return this;
    }

    /**
     * Sets whether <a href="https://tools.ietf.org/html/rfc7932">Brotli</a> compression is enabled.
     * If enabled, Brotli will be advertised in Accept-Encoding request headers. Defaults to
     * disabled.
     *
     * @param value {@code true} to enable Brotli, {@code false} to disable.
     * @return the builder to facilitate chaining.
     */
    public Builder enableBrotli(boolean value) {
      mBuilderDelegate.enableBrotli(value);
      return this;
    }

    /**
     * Setting to disable HTTP cache. Some data may still be temporarily stored in memory. Passed to
     * {@link #enableHttpCache}.
     */
    public static final int HTTP_CACHE_DISABLED = 0;

    /**
     * Setting to enable in-memory HTTP cache, including HTTP data. Passed to {@link
     * #enableHttpCache}.
     */
    public static final int HTTP_CACHE_IN_MEMORY = 1;

    /**
     * Setting to enable on-disk cache, excluding HTTP data. {@link #setStoragePath} must be called
     * prior to passing this constant to {@link #enableHttpCache}.
     */
    public static final int HTTP_CACHE_DISK_NO_HTTP = 2;

    /**
     * Setting to enable on-disk cache, including HTTP data. {@link #setStoragePath} must be called
     * prior to passing this constant to {@link #enableHttpCache}.
     */
    public static final int HTTP_CACHE_DISK = 3;

    /**
     * Enables or disables caching of HTTP data and other information like QUIC server information.
     *
     * @param cacheMode control location and type of cached data. Must be one of {@link
     *     #HTTP_CACHE_DISABLED HTTP_CACHE_*}.
     * @param maxSize maximum size in bytes used to cache data (advisory and maybe exceeded at
     *     times).
     * @return the builder to facilitate chaining.
     */
    public Builder enableHttpCache(int cacheMode, long maxSize) {
      mBuilderDelegate.enableHttpCache(cacheMode, maxSize);
      return this;
    }

    /**
     * Adds hint that {@code host} supports QUIC. Note that {@link #enableHttpCache enableHttpCache}
     * ({@link #HTTP_CACHE_DISK}) is needed to take advantage of 0-RTT connection establishment
     * between sessions.
     *
     * @param host hostname of the server that supports QUIC.
     * @param port host of the server that supports QUIC.
     * @param alternatePort alternate port to use for QUIC.
     * @return the builder to facilitate chaining.
     */
    public Builder addQuicHint(String host, int port, int alternatePort) {
      mBuilderDelegate.addQuicHint(host, port, alternatePort);
      return this;
    }

    /**
     * Pins a set of public keys for a given host. By pinning a set of public keys, {@code
     * pinsSha256}, communication with {@code hostName} is required to authenticate with a
     * certificate with a public key from the set of pinned ones. An app can pin the public key of
     * the root certificate, any of the intermediate certificates or the end-entry certificate.
     * Authentication will fail and secure communication will not be established if none of the
     * public keys is present in the host's certificate chain, even if the host attempts to
     * authenticate with a certificate allowed by the device's trusted store of certificates.
     *
     * <p>Calling this method multiple times with the same host name overrides the previously set
     * pins for the host.
     *
     * <p>More information about the public key pinning can be found in <a
     * href="https://tools.ietf.org/html/rfc7469">RFC 7469</a>.
     *
     * @param hostName name of the host to which the public keys should be pinned. A host that
     *     consists only of digits and the dot character is treated as invalid.
     * @param pinsSha256 a set of pins. Each pin is the SHA-256 cryptographic hash of the
     *     DER-encoded ASN.1 representation of the Subject Public Key Info (SPKI) of the host's
     *     X.509 certificate. Use {@link java.security.cert.Certificate#getPublicKey()
     *     Certificate.getPublicKey()} and {@link java.security.Key#getEncoded() Key.getEncoded()}
     *     to obtain DER-encoded ASN.1 representation of the SPKI. Although, the method does not
     *     mandate the presence of the backup pin that can be used if the control of the primary
     *     private key has been lost, it is highly recommended to supply one.
     * @param includeSubdomains indicates whether the pinning policy should be applied to subdomains
     *     of {@code hostName}.
     * @param expirationDate specifies the expiration date for the pins.
     * @return the builder to facilitate chaining.
     * @throws NullPointerException if any of the input parameters are {@code null}.
     * @throws IllegalArgumentException if the given host name is invalid or {@code pinsSha256}
     *     contains a byte array that does not represent a valid SHA-256 hash.
     */
    public Builder addPublicKeyPins(String hostName, Set<byte[]> pinsSha256,
                                    boolean includeSubdomains, Date expirationDate) {
      mBuilderDelegate.addPublicKeyPins(hostName, pinsSha256, includeSubdomains, expirationDate);
      return this;
    }

    /**
     * Enables or disables public key pinning bypass for local trust anchors. Disabling the bypass
     * for local trust anchors is highly discouraged since it may prohibit the app from
     * communicating with the pinned hosts. E.g., a user may want to send all traffic through an SSL
     * enabled proxy by changing the device proxy settings and adding the proxy certificate to the
     * list of local trust anchor. Disabling the bypass will most likely prevent the app from
     * sending any traffic to the pinned hosts. For more information see 'How does key pinning
     * interact with local proxies and filters?' at
     * https://www.chromium.org/Home/chromium-security/security-faq
     *
     * @param value {@code true} to enable the bypass, {@code false} to disable.
     * @return the builder to facilitate chaining.
     */
    public Builder enablePublicKeyPinningBypassForLocalTrustAnchors(boolean value) {
      mBuilderDelegate.enablePublicKeyPinningBypassForLocalTrustAnchors(value);
      return this;
    }

    /**
     * Build a {@link CronetEngine} using this builder's configuration.
     *
     * @return constructed {@link CronetEngine}.
     */
    public CronetEngine build() { return mBuilderDelegate.build(); }

    /**
     * Creates an implementation of {@link ICronetEngineBuilder} that can be used to delegate the
     * builder calls to. The method uses {@link CronetProvider} to obtain the list of available
     * providers.
     *
     * @param context Android Context to use.
     * @return the created {@code ICronetEngineBuilder}.
     */
    private static ICronetEngineBuilder createBuilderDelegate(Context context) {
      List<CronetProvider> providers = new ArrayList<>(CronetProvider.getAllProviders(context));
      CronetProvider provider = getEnabledCronetProviders(context, providers).get(0);
      if (Log.isLoggable(TAG, Log.DEBUG)) {
        Log.d(TAG,
              String.format("Using '%s' provider for creating CronetEngine.Builder.", provider));
      }
      return provider.createBuilder().mBuilderDelegate;
    }

    /**
     * Returns the list of available and enabled {@link CronetProvider}. The returned list is sorted
     * based on the provider versions and types.
     *
     * @param context Android Context to use.
     * @param providers the list of enabled and disabled providers to filter out and sort.
     * @return the sorted list of enabled providers. The list contains at least one provider.
     * @throws RuntimeException is the list of providers is empty or all of the providers are
     *     disabled.
     */
    @VisibleForTesting
    static List<CronetProvider> getEnabledCronetProviders(Context context,
                                                          List<CronetProvider> providers) {
      // Check that there is at least one available provider.
      if (providers.size() == 0) {
        throw new RuntimeException("Unable to find any Cronet provider."
                                   + " Have you included all necessary jars?");
      }

      // Exclude disabled providers from the list.
      for (Iterator<CronetProvider> i = providers.iterator(); i.hasNext();) {
        CronetProvider provider = i.next();
        if (!provider.isEnabled()) {
          i.remove();
        }
      }

      // Check that there is at least one enabled provider.
      if (providers.size() == 0) {
        throw new RuntimeException("All available Cronet providers are disabled."
                                   + " A provider should be enabled before it can be used.");
      }

      // Sort providers based on version and type.
      Collections.sort(providers, new Comparator<CronetProvider>() {
        @Override
        public int compare(CronetProvider p1, CronetProvider p2) {
          // The fallback provider should always be at the end of the list.
          if (CronetProvider.PROVIDER_NAME_FALLBACK.equals(p1.getName())) {
            return 1;
          }
          if (CronetProvider.PROVIDER_NAME_FALLBACK.equals(p2.getName())) {
            return -1;
          }
          // A provider with higher version should go first.
          return -compareVersions(p1.getVersion(), p2.getVersion());
        }
      });
      return providers;
    }

    /**
     * Compares two strings that contain versions. The string should only contain dot-separated
     * segments that contain an arbitrary number of digits digits [0-9].
     *
     * @param s1 the first string.
     * @param s2 the second string.
     * @return -1 if s1<s2, +1 if s1>s2 and 0 if s1=s2. If two versions are equal, the version with
     *     the higher number of segments is considered to be higher.
     * @throws IllegalArgumentException if any of the strings contains an illegal version number.
     */
    @VisibleForTesting
    static int compareVersions(String s1, String s2) {
      if (s1 == null || s2 == null) {
        throw new IllegalArgumentException("The input values cannot be null");
      }
      String[] s1segments = s1.split("\\.");
      String[] s2segments = s2.split("\\.");
      for (int i = 0; i < s1segments.length && i < s2segments.length; i++) {
        try {
          int s1segment = Integer.parseInt(s1segments[i]);
          int s2segment = Integer.parseInt(s2segments[i]);
          if (s1segment != s2segment) {
            return Integer.signum(s1segment - s2segment);
          }
        } catch (NumberFormatException e) {
          throw new IllegalArgumentException("Unable to convert version segments into"
                                                 + " integers: " + s1segments[i] + " & " +
                                                 s2segments[i],
                                             e);
        }
      }
      return Integer.signum(s1segments.length - s2segments.length);
    }
  }

  /** @return a human-readable version string of the engine. */
  public abstract String getVersionString();

  /**
   * Shuts down the {@link CronetEngine} if there are no active requests, otherwise throws an
   * exception.
   *
   * <p>Cannot be called on network thread - the thread Cronet calls into Executor on (which is
   * different from the thread the Executor invokes callbacks on). May block until all the {@code
   * CronetEngine}'s resources have been cleaned up.
   */
  public abstract void shutdown();

  /**
   * Starts NetLog logging to a file. The NetLog will contain events emitted by all live
   * CronetEngines. The NetLog is useful for debugging. The file can be viewed using a Chrome
   * browser navigated to chrome://net-internals/#import
   *
   * @param fileName the complete file path. It must not be empty. If the file exists, it is
   *     truncated before starting. If actively logging, this method is ignored.
   * @param logAll {@code true} to include basic events, user cookies, credentials and all
   *     transferred bytes in the log. This option presents a privacy risk, since it exposes the
   *     user's credentials, and should only be used with the user's consent and in situations where
   *     the log won't be public. {@code false} to just include basic events.
   */
  public abstract void startNetLogToFile(String fileName, boolean logAll);

  /**
   * Stops NetLog logging and flushes file to disk. If a logging session is not in progress, this
   * call is ignored.
   */
  public abstract void stopNetLog();

  /**
   * Returns differences in metrics collected by Cronet since the last call to this method.
   *
   * <p>Cronet collects these metrics globally. This means deltas returned by {@code
   * getGlobalMetricsDeltas()} will include measurements of requests processed by other {@link
   * CronetEngine} instances. Since this function returns differences in metrics collected since the
   * last call, and these metrics are collected globally, a call to any {@code CronetEngine}
   * instance's {@code getGlobalMetricsDeltas()} method will affect the deltas returned by any other
   * {@code CronetEngine} instance's {@code getGlobalMetricsDeltas()}.
   *
   * <p>Cronet starts collecting these metrics after the first call to {@code
   * getGlobalMetricsDeltras()}, so the first call returns no useful data as no metrics have yet
   * been collected.
   *
   * @return differences in metrics collected by Cronet, since the last call to {@code
   *     getGlobalMetricsDeltas()}, serialized as a <a
   *     href=https://developers.google.com/protocol-buffers>protobuf </a>.
   */
  public abstract byte[] getGlobalMetricsDeltas();

  /**
   * Establishes a new connection to the resource specified by the {@link URL} {@code url}.
   *
   * <p><b>Note:</b> Cronet's {@link java.net.HttpURLConnection} implementation is subject to
   * certain limitations, see {@link #createURLStreamHandlerFactory} for details.
   *
   * @param url URL of resource to connect to.
   * @return an {@link java.net.HttpURLConnection} instance implemented by this CronetEngine.
   * @throws IOException if an error occurs while opening the connection.
   */
  public abstract URLConnection openConnection(URL url) throws IOException;

  /**
   * Creates a {@link URLStreamHandlerFactory} to handle HTTP and HTTPS traffic. An instance of this
   * class can be installed via {@link URL#setURLStreamHandlerFactory} thus using this CronetEngine
   * by default for all requests created via {@link URL#openConnection}.
   *
   * <p>Cronet does not use certain HTTP features provided via the system:
   *
   * <ul>
   *   <li>the HTTP cache installed via {@link HttpResponseCache#install(java.io.File, long)
   *       HttpResponseCache.install()}
   *   <li>the HTTP authentication method installed via {@link java.net.Authenticator#setDefault}
   *   <li>the HTTP cookie storage installed via {@link java.net.CookieHandler#setDefault}
   * </ul>
   *
   * <p>While Cronet supports and encourages requests using the HTTPS protocol, Cronet does not
   * provide support for the {@link HttpsURLConnection} API. This lack of support also includes not
   * using certain HTTPS features provided via the system:
   *
   * <ul>
   *   <li>the HTTPS hostname verifier installed via {@link
   *       HttpsURLConnection#setDefaultHostnameVerifier(javax.net.ssl.HostnameVerifier)
   *       HttpsURLConnection.setDefaultHostnameVerifier()}
   *   <li>the HTTPS socket factory installed via {@link
   *       HttpsURLConnection#setDefaultSSLSocketFactory(javax.net.ssl.SSLSocketFactory)
   *       HttpsURLConnection.setDefaultSSLSocketFactory()}
   * </ul>
   *
   * @return an {@link URLStreamHandlerFactory} instance implemented by this CronetEngine.
   */
  public abstract URLStreamHandlerFactory createURLStreamHandlerFactory();

  /**
   * Creates a builder for {@link UrlRequest}. All callbacks for generated {@link UrlRequest}
   * objects will be invoked on {@code executor}'s threads. {@code executor} must not run tasks on
   * the thread calling {@link Executor#execute} to prevent blocking networking operations and
   * causing exceptions during shutdown.
   *
   * @param url URL for the generated requests.
   * @param callback callback object that gets invoked on different events.
   * @param executor {@link Executor} on which all callbacks will be invoked.
   */
  public abstract UrlRequest.Builder newUrlRequestBuilder(String url, UrlRequest.Callback callback,
                                                          Executor executor);
}
