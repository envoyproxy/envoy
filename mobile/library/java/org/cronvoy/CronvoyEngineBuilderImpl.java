package org.cronvoy;

import static android.os.Process.THREAD_PRIORITY_LOWEST;

import android.content.Context;
import android.util.Base64;
import androidx.annotation.IntDef;
import java.io.File;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.net.IDN;
import java.util.Date;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;
import org.chromium.net.CronetEngine;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

/** Implementation of {@link ICronetEngineBuilder} that builds Envoy-Mobile based Cronet engine. */
final class CronvoyEngineBuilderImpl extends ICronetEngineBuilder {

  /** A hint that a host supports QUIC. */
  final static class QuicHint {
    // The host.
    final String host;
    // Port of the server that supports QUIC.
    final int port;
    // Alternate protocol port.
    final int alternatePort;

    QuicHint(String host, int port, int alternatePort) {
      this.host = host;
      this.port = port;
      this.alternatePort = alternatePort;
    }
  }

  /** A public key pin. */
  final static class Pkp {
    // Host to pin for.
    final String host;
    // Array of SHA-256 hashes of keys.
    final byte[][] hashes;
    // Should pin apply to subdomains?
    final boolean includeSubdomains;
    // When the pin expires.
    final Date expirationDate;

    Pkp(String host, byte[][] hashes, boolean includeSubdomains, Date expirationDate) {
      this.host = host;
      this.hashes = hashes;
      this.includeSubdomains = includeSubdomains;
      this.expirationDate = expirationDate;
    }
  }

  private static final Pattern INVALID_PKP_HOST_NAME = Pattern.compile("^[0-9\\.]*$");

  private static final int INVALID_THREAD_PRIORITY = THREAD_PRIORITY_LOWEST + 1;

  // Private fields are simply storage of configuration for the resulting CronetEngine.
  // See setters below for verbose descriptions.
  private final Context applicationContext;
  private final List<CronvoyEngineBuilderImpl.QuicHint> quicHints = new LinkedList<>();
  private final List<CronvoyEngineBuilderImpl.Pkp> pkps = new LinkedList<>();
  private boolean publicKeyPinningBypassForLocalTrustAnchorsEnabled;
  private String userAgent;
  private String storagePath;
  private boolean quicEnabled;
  private boolean http2Enabled;
  private boolean brotiEnabled;
  private boolean disableCache;
  private int httpCacheMode;
  private long httpCacheMaxSize;
  private String experimentalOptions;
  private long mockCertVerifier;
  private boolean networkQualityEstimatorEnabled;
  private int threadPriority = INVALID_THREAD_PRIORITY;

  /**
   * Default config enables SPDY and QUIC, disables SDCH and HTTP cache.
   *
   * @param context Android {@link Context} for engine to use.
   */
  public CronvoyEngineBuilderImpl(Context context) {
    applicationContext = context.getApplicationContext();
    enableQuic(true);
    enableHttp2(true);
    enableBrotli(false);
    enableHttpCache(CronetEngine.Builder.HTTP_CACHE_DISABLED, 0);
    enableNetworkQualityEstimator(false);
    enablePublicKeyPinningBypassForLocalTrustAnchors(true);
  }

  @Override
  public String getDefaultUserAgent() {
    return UserAgent.from(applicationContext);
  }

  @Override
  public CronvoyEngineBuilderImpl setUserAgent(String userAgent) {
    this.userAgent = userAgent;
    return this;
  }

  String getUserAgent() { return userAgent; }

  @Override
  public CronvoyEngineBuilderImpl setStoragePath(String value) {
    if (!new File(value).isDirectory()) {
      throw new IllegalArgumentException("Storage path must be set to existing directory");
    }
    storagePath = value;
    return this;
  }

  String storagePath() { return storagePath; }

  @Override
  public CronvoyEngineBuilderImpl setLibraryLoader(CronetEngine.Builder.LibraryLoader loader) {
    // |CronvoyEngineBuilderImpl| is an abstract class that is used by concrete builder
    // implementations, including the Java Cronet engine builder; therefore, the implementation
    // of this method should be "no-op". Subclasses that care about the library loader
    // should override this method.
    return this;
  }

  @Override
  public CronvoyEngineBuilderImpl enableQuic(boolean value) {
    quicEnabled = value;
    return this;
  }

  boolean quicEnabled() { return quicEnabled; }

  /**
   * Constructs default QUIC User Agent Id string including application name and Cronet version.
   * Returns empty string if QUIC is not enabled.
   *
   * @return QUIC User Agent ID string.
   */
  String getDefaultQuicUserAgentId() {
    return quicEnabled ? UserAgent.getQuicUserAgentIdFrom(applicationContext) : "";
  }

  @Override
  public CronvoyEngineBuilderImpl enableHttp2(boolean value) {
    http2Enabled = value;
    return this;
  }

  boolean http2Enabled() { return http2Enabled; }

  @Override
  public CronvoyEngineBuilderImpl enableSdch(boolean value) {
    return this;
  }

  @Override
  public CronvoyEngineBuilderImpl enableBrotli(boolean value) {
    brotiEnabled = value;
    return this;
  }

  boolean brotliEnabled() { return brotiEnabled; }

  @IntDef({CronetEngine.Builder.HTTP_CACHE_DISABLED, CronetEngine.Builder.HTTP_CACHE_IN_MEMORY,
           CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP, CronetEngine.Builder.HTTP_CACHE_DISK})
  @Retention(RetentionPolicy.SOURCE)
  public @interface HttpCacheSetting {}

  @Override
  public CronvoyEngineBuilderImpl
  enableHttpCache(@CronvoyEngineBuilderImpl.HttpCacheSetting int cacheMode, long maxSize) {
    if (cacheMode == CronetEngine.Builder.HTTP_CACHE_DISK ||
        cacheMode == CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP) {
      if (storagePath() == null) {
        throw new IllegalArgumentException("Storage path must be set");
      }
    } else {
      if (storagePath() != null) {
        throw new IllegalArgumentException("Storage path must not be set");
      }
    }
    disableCache = (cacheMode == CronetEngine.Builder.HTTP_CACHE_DISABLED ||
                    cacheMode == CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP);
    httpCacheMaxSize = maxSize;

    switch (cacheMode) {
    case CronetEngine.Builder.HTTP_CACHE_DISABLED:
      httpCacheMode = CronetEngine.Builder.HTTP_CACHE_DISABLED;
      break;
    case CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP:
    case CronetEngine.Builder.HTTP_CACHE_DISK:
    case CronetEngine.Builder.HTTP_CACHE_IN_MEMORY:
    default:
      throw new IllegalArgumentException("Unknown cache mode");
    }
    return this;
  }

  boolean cacheDisabled() { return disableCache; }

  long httpCacheMaxSize() { return httpCacheMaxSize; }

  int httpCacheMode() { return httpCacheMode; }

  @Override
  public CronvoyEngineBuilderImpl addQuicHint(String host, int port, int alternatePort) {
    if (host.contains("/")) {
      throw new IllegalArgumentException("Illegal QUIC Hint Host: " + host);
    }
    quicHints.add(new CronvoyEngineBuilderImpl.QuicHint(host, port, alternatePort));
    return this;
  }

  List<CronvoyEngineBuilderImpl.QuicHint> quicHints() { return quicHints; }

  @Override
  public CronvoyEngineBuilderImpl addPublicKeyPins(String hostName, Set<byte[]> pinsSha256,
                                                   boolean includeSubdomains, Date expirationDate) {
    if (hostName == null) {
      throw new NullPointerException("The hostname cannot be null");
    }
    if (pinsSha256 == null) {
      throw new NullPointerException("The set of SHA256 pins cannot be null");
    }
    if (expirationDate == null) {
      throw new NullPointerException("The pin expiration date cannot be null");
    }
    String idnHostName = validateHostNameForPinningAndConvert(hostName);
    // Convert the pin to BASE64 encoding to remove duplicates.
    Map<String, byte[]> hashes = new HashMap<>();
    for (byte[] pinSha256 : pinsSha256) {
      if (pinSha256 == null || pinSha256.length != 32) {
        throw new IllegalArgumentException("Public key pin is invalid");
      }
      hashes.put(Base64.encodeToString(pinSha256, 0), pinSha256);
    }
    // Add new element to PKP list.
    pkps.add(new CronvoyEngineBuilderImpl.Pkp(idnHostName,
                                              hashes.values().toArray(new byte[hashes.size()][]),
                                              includeSubdomains, expirationDate));
    return this;
  }

  /**
   * Returns list of public key pins.
   *
   * @return list of public key pins.
   */
  List<CronvoyEngineBuilderImpl.Pkp> publicKeyPins() { return pkps; }

  @Override
  public CronvoyEngineBuilderImpl enablePublicKeyPinningBypassForLocalTrustAnchors(boolean value) {
    publicKeyPinningBypassForLocalTrustAnchorsEnabled = value;
    return this;
  }

  boolean publicKeyPinningBypassForLocalTrustAnchorsEnabled() {
    return publicKeyPinningBypassForLocalTrustAnchorsEnabled;
  }

  /**
   * Checks whether a given string represents a valid host name for PKP and converts it to ASCII
   * Compatible Encoding representation according to RFC 1122, RFC 1123 and RFC 3490. This method is
   * more restrictive than required by RFC 7469. Thus, a host that contains digits and the dot
   * character only is considered invalid.
   *
   * <p>Note: Currently Cronet doesn't have native implementation of host name validation that can
   * be used. There is code that parses a provided URL but doesn't ensure its correctness. The
   * implementation relies on {@code getaddrinfo} function.
   *
   * @param hostName host name to check and convert.
   * @return true if the string is a valid host name.
   * @throws IllegalArgumentException if the the given string does not represent a valid hostname.
   */
  private static String validateHostNameForPinningAndConvert(String hostName)
      throws IllegalArgumentException {
    if (INVALID_PKP_HOST_NAME.matcher(hostName).matches()) {
      throw new IllegalArgumentException(
          "Hostname " + hostName + " is illegal."
          + " A hostname should not consist of digits and/or dots only.");
    }
    // Workaround for crash, see crbug.com/634914
    if (hostName.length() > 255) {
      throw new IllegalArgumentException(
          "Hostname " + hostName + " is too long."
          + " The name of the host does not comply with RFC 1122 and RFC 1123.");
    }
    try {
      return IDN.toASCII(hostName, IDN.USE_STD3_ASCII_RULES);
    } catch (IllegalArgumentException ex) {
      throw new IllegalArgumentException(
          "Hostname " + hostName + " is illegal."
          + " The name of the host does not comply with RFC 1122 and RFC 1123.");
    }
  }

  @Override
  public CronvoyEngineBuilderImpl setExperimentalOptions(String options) {
    experimentalOptions = options;
    return this;
  }

  public String experimentalOptions() { return experimentalOptions; }

  /**
   * Sets a native MockCertVerifier for testing. See {@code MockCertVerifier.createMockCertVerifier}
   * for a method that can be used to create a MockCertVerifier.
   *
   * @param mockCertVerifier pointer to native MockCertVerifier.
   * @return the builder to facilitate chaining.
   */
  CronvoyEngineBuilderImpl setMockCertVerifierForTesting(long mockCertVerifier) {
    this.mockCertVerifier = mockCertVerifier;
    return this;
  }

  long mockCertVerifier() { return mockCertVerifier; }

  /** @return true if the network quality estimator has been enabled for this builder. */
  boolean networkQualityEstimatorEnabled() { return networkQualityEstimatorEnabled; }

  @Override
  public CronvoyEngineBuilderImpl enableNetworkQualityEstimator(boolean value) {
    networkQualityEstimatorEnabled = value;
    return this;
  }

  @Override
  public CronvoyEngineBuilderImpl setThreadPriority(int priority) {
    if (priority > THREAD_PRIORITY_LOWEST || priority < -20) {
      throw new IllegalArgumentException("Thread priority invalid");
    }
    threadPriority = priority;
    return this;
  }

  /**
   * @return thread priority provided by user, or {@code defaultThreadPriority} if none provided.
   */
  int threadPriority(int defaultThreadPriority) {
    return threadPriority == INVALID_THREAD_PRIORITY ? defaultThreadPriority : threadPriority;
  }

  /**
   * Returns {@link Context} for builder.
   *
   * @return {@link Context} for builder.
   */
  Context getContext() { return applicationContext; }

  @Override
  public ExperimentalCronetEngine build() {
    if (getUserAgent() == null) {
      setUserAgent(getDefaultUserAgent());
    }
    return new CronvoyEngine(this);
  }
}
