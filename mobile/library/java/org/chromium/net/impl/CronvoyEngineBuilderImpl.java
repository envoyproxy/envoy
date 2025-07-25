package org.chromium.net.impl;

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
import org.chromium.net.ICronetEngineBuilder;
import org.chromium.net.impl.Annotations.HttpCacheType;

import io.envoyproxy.envoymobile.engine.EnvoyEngine;

/** Implementation of {@link ICronetEngineBuilder} that builds Envoy-Mobile based Cronet engine. */
public abstract class CronvoyEngineBuilderImpl extends ICronetEngineBuilder {

  /** A public key pin. */
  final static class Pkp {
    // Host to pin for.
    final String mHost;
    // Array of SHA-256 hashes of keys.
    final byte[][] mHashes;
    // Should pin apply to subdomains?
    final boolean mIncludeSubdomains;
    // When the pin expires.
    final Date mExpirationDate;

    Pkp(String host, byte[][] hashes, boolean includeSubdomains, Date expirationDate) {
      mHost = host;
      mHashes = hashes;
      mIncludeSubdomains = includeSubdomains;
      mExpirationDate = expirationDate;
    }
  }

  private static final Pattern INVALID_PKP_HOST_NAME = Pattern.compile("^[0-9\\.]*$");

  private static final int INVALID_THREAD_PRIORITY = THREAD_PRIORITY_LOWEST + 1;

  // Private fields are simply storage of configuration for the resulting CronetEngine.
  // See setters below for verbose descriptions.
  private final Context mApplicationContext;
  private final Map<String, Integer> mQuicHints = new HashMap<>();
  private final List<String> mQuicCanonicalSuffixes = new LinkedList<>();
  private final List<Pkp> mPkps = new LinkedList<>();
  private boolean mPublicKeyPinningBypassForLocalTrustAnchorsEnabled;
  private String mUserAgent;
  private String mStoragePath;
  private boolean mQuicEnabled;
  private String mQuicConnectionOptions = "";
  private String mQuicClientConnectionOptions = "";
  private boolean mHttp2Enabled;
  private boolean mBrotiEnabled;
  private int mNumTimeoutsToTriggerPortMigration;
  private boolean mDisableCache;
  private int mHttpCacheMode;
  private long mHttpCacheMaxSize;
  private String mExperimentalOptions;
  private boolean mNetworkQualityEstimatorEnabled;
  private int mThreadPriority = INVALID_THREAD_PRIORITY;
  private EnvoyEngine.LogLevel mLogLevel = EnvoyEngine.LogLevel.OFF;
  private CronvoyLogger mCronvoyLogger = new CronvoyLogger();

  /**
   * Default config enables SPDY and QUIC, disables SDCH and HTTP cache.
   *
   * @param context Android {@link Context} for engine to use.
   */
  CronvoyEngineBuilderImpl(Context context) {
    mApplicationContext = context.getApplicationContext();
    enableQuic(true);
    enableHttp2(true);
    enableBrotli(false);
    enableHttpCache(CronetEngine.Builder.HTTP_CACHE_DISABLED, 0);
    enableNetworkQualityEstimator(false);
    enablePublicKeyPinningBypassForLocalTrustAnchors(true);
  }

  @Override
  public String getDefaultUserAgent() {
    return CronvoyUserAgent.from(mApplicationContext);
  }

  @Override
  public CronvoyEngineBuilderImpl setUserAgent(String userAgent) {
    mUserAgent = userAgent;
    return this;
  }

  String getUserAgent() { return mUserAgent; }

  @Override
  public CronvoyEngineBuilderImpl setStoragePath(String value) {
    if (!new File(value).isDirectory()) {
      throw new IllegalArgumentException("Storage path must be set to existing directory");
    }
    mStoragePath = value;
    return this;
  }

  String storagePath() { return mStoragePath; }

  @Override
  public CronvoyEngineBuilderImpl setLibraryLoader(CronetEngine.Builder.LibraryLoader loader) {
    // |CronvoyEngineBuilderImpl| is an abstract class that is used by concrete builder
    // implementations, including the Java Cronet engine builder; therefore, the implementation
    // of this method should be "no-op". Subclasses that care about the library loader
    // should override this method.
    return this;
  }

  /**
   * Default implementation of the method that returns {@code null}.
   */
  CronvoyVersionSafeCallbacks.LibraryLoader libraryLoader() { return null; }

  @Override
  public CronvoyEngineBuilderImpl enableQuic(boolean value) {
    mQuicEnabled = value;
    return this;
  }

  boolean quicEnabled() { return mQuicEnabled; }

  public CronvoyEngineBuilderImpl setQuicConnectionOptions(String options) {
    mQuicConnectionOptions = options;
    return this;
  }

  String quicConnectionOptions() { return mQuicConnectionOptions; }

  public CronvoyEngineBuilderImpl setQuicClientConnectionOptions(String options) {
    mQuicClientConnectionOptions = options;
    return this;
  }

  String quicClientConnectionOptions() { return mQuicClientConnectionOptions; }

  /**
   * Constructs default QUIC User Agent Id string including application name and Cronet version.
   * Returns empty string if QUIC is not enabled.
   *
   * @return QUIC User Agent ID string.
   */
  String getDefaultQuicUserAgentId() {
    return mQuicEnabled ? CronvoyUserAgent.getQuicUserAgentIdFrom(mApplicationContext) : "";
  }

  @Override
  public CronvoyEngineBuilderImpl enableHttp2(boolean value) {
    mHttp2Enabled = value;
    return this;
  }

  boolean http2Enabled() { return mHttp2Enabled; }

  @Override
  public CronvoyEngineBuilderImpl enableSdch(boolean value) {
    return this;
  }

  @Override
  public CronvoyEngineBuilderImpl enableBrotli(boolean value) {
    mBrotiEnabled = value;
    return this;
  }

  boolean brotliEnabled() { return mBrotiEnabled; }

  @IntDef({CronetEngine.Builder.HTTP_CACHE_DISABLED, CronetEngine.Builder.HTTP_CACHE_IN_MEMORY,
           CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP, CronetEngine.Builder.HTTP_CACHE_DISK})
  @Retention(RetentionPolicy.SOURCE)
  public @interface HttpCacheSetting {}

  @Override
  public CronvoyEngineBuilderImpl enableHttpCache(@HttpCacheSetting int cacheMode, long maxSize) {
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
    mDisableCache = (cacheMode == CronetEngine.Builder.HTTP_CACHE_DISABLED ||
                     cacheMode == CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP);
    mHttpCacheMaxSize = maxSize;

    switch (cacheMode) {
    case CronetEngine.Builder.HTTP_CACHE_DISABLED:
      mHttpCacheMode = HttpCacheType.DISABLED;
      break;
    case CronetEngine.Builder.HTTP_CACHE_DISK_NO_HTTP:
    case CronetEngine.Builder.HTTP_CACHE_DISK:
      mHttpCacheMode = HttpCacheType.DISK;
      break;
    case CronetEngine.Builder.HTTP_CACHE_IN_MEMORY:
      mHttpCacheMode = HttpCacheType.MEMORY;
      break;
    default:
      throw new IllegalArgumentException("Unknown cache mode");
    }
    return this;
  }

  boolean cacheDisabled() { return mDisableCache; }

  long httpCacheMaxSize() { return mHttpCacheMaxSize; }

  int httpCacheMode() { return mHttpCacheMode; }

  @Override
  public CronvoyEngineBuilderImpl addQuicHint(String host, int port, int alternatePort) {
    if (host.contains("/")) {
      throw new IllegalArgumentException("Illegal QUIC Hint Host: " + host);
    }
    mQuicHints.put(host, port);
    return this;
  }

  Map<String, Integer> quicHints() { return mQuicHints; }

  public CronvoyEngineBuilderImpl addQuicCanonicalSuffix(String suffix) {
    mQuicCanonicalSuffixes.add(suffix);
    return this;
  }

  List<String> quicCanonicalSuffixes() { return mQuicCanonicalSuffixes; }

  public CronvoyEngineBuilderImpl
  setNumTimeoutsToTriggerPortMigration(int numTimeoutsToTriggerPortMigration) {
    mNumTimeoutsToTriggerPortMigration = numTimeoutsToTriggerPortMigration;
    return this;
  }

  int numTimeoutsToTriggerPortMigration() { return mNumTimeoutsToTriggerPortMigration; }

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
    mPkps.add(new Pkp(idnHostName, hashes.values().toArray(new byte[hashes.size()][]),
                      includeSubdomains, expirationDate));
    return this;
  }

  /**
   * Returns list of public key pins.
   *
   * @return list of public key pins.
   */
  List<Pkp> publicKeyPins() { return mPkps; }

  @Override
  public CronvoyEngineBuilderImpl enablePublicKeyPinningBypassForLocalTrustAnchors(boolean value) {
    mPublicKeyPinningBypassForLocalTrustAnchorsEnabled = value;
    return this;
  }

  boolean publicKeyPinningBypassForLocalTrustAnchorsEnabled() {
    return mPublicKeyPinningBypassForLocalTrustAnchorsEnabled;
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
    mExperimentalOptions = options;
    return this;
  }

  public String experimentalOptions() { return mExperimentalOptions; }

  /**
   * @return true if the network quality estimator has been enabled for
   * this builder.
   */
  boolean networkQualityEstimatorEnabled() { return mNetworkQualityEstimatorEnabled; }

  @Override
  public CronvoyEngineBuilderImpl enableNetworkQualityEstimator(boolean value) {
    mNetworkQualityEstimatorEnabled = value;
    return this;
  }

  @Override
  public CronvoyEngineBuilderImpl setThreadPriority(int priority) {
    if (priority > THREAD_PRIORITY_LOWEST || priority < -20) {
      throw new IllegalArgumentException("Thread priority invalid");
    }
    mThreadPriority = priority;
    return this;
  }

  /**
   * @return thread priority provided by user, or {@code defaultThreadPriority} if none provided.
   */
  int threadPriority(int defaultThreadPriority) {
    return mThreadPriority == INVALID_THREAD_PRIORITY ? defaultThreadPriority : mThreadPriority;
  }

  /**
   * Returns {@link Context} for builder.
   *
   * @return {@link Context} for builder.
   */
  Context getContext() { return mApplicationContext; }

  /** Sets the log level. */
  public CronvoyEngineBuilderImpl setLogLevel(EnvoyEngine.LogLevel logLevel) {
    this.mLogLevel = logLevel;
    return this;
  }

  /** Gets the log level. It defaults to `OFF` when not set. */
  public EnvoyEngine.LogLevel getLogLevel() { return mLogLevel; }

  /** Sets the {@link io.envoyproxy.envoymobile.engine.types.EnvoyLogger}. */
  public CronvoyEngineBuilderImpl setLogger(CronvoyLogger cronvoyLogger) {
    this.mCronvoyLogger = cronvoyLogger;
    return this;
  }

  /** Gets the {@link org.chromium.net.impl.CronvoyLogger} implementation. */
  public CronvoyLogger getLogger() { return mCronvoyLogger; }
}
