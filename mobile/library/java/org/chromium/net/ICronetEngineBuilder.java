package org.chromium.net;

import java.util.Date;
import java.util.Set;

/**
 * Defines methods that the actual implementation of {@link CronetEngine.Builder} has to implement.
 * {@code CronetEngine.Builder} uses this interface to delegate the calls. For the documentation of
 * individual methods, please see the identically named methods in {@link
 * org.chromium.net.CronetEngine.Builder} and {@link
 * org.chromium.net.ExperimentalCronetEngine.Builder}.
 *
 * <p>{@hide internal class}
 */
public abstract class ICronetEngineBuilder {
  // Public API methods.
  public abstract ICronetEngineBuilder addPublicKeyPins(String hostName, Set<byte[]> pinsSha256,
                                                        boolean includeSubdomains,
                                                        Date expirationDate);

  public abstract ICronetEngineBuilder addQuicHint(String host, int port, int alternatePort);

  public abstract ICronetEngineBuilder enableHttp2(boolean value);

  public abstract ICronetEngineBuilder enableHttpCache(int cacheMode, long maxSize);

  public abstract ICronetEngineBuilder
  enablePublicKeyPinningBypassForLocalTrustAnchors(boolean value);

  public abstract ICronetEngineBuilder enableQuic(boolean value);

  public abstract ICronetEngineBuilder enableSdch(boolean value);

  public ICronetEngineBuilder enableBrotli(boolean value) {
    // Do nothing for older implementations.
    return this;
  }

  public abstract ICronetEngineBuilder setExperimentalOptions(String options);

  public abstract ICronetEngineBuilder setLibraryLoader(CronetEngine.Builder.LibraryLoader loader);

  public abstract ICronetEngineBuilder setStoragePath(String value);

  public abstract ICronetEngineBuilder setUserAgent(String userAgent);

  public abstract String getDefaultUserAgent();

  public abstract ExperimentalCronetEngine build();

  // Experimental API methods.
  //
  // Note: all experimental API methods should have default implementation. This will allow
  // removing the experimental methods from the implementation layer without breaking
  // the client.

  public ICronetEngineBuilder enableNetworkQualityEstimator(boolean value) { return this; }

  public ICronetEngineBuilder setThreadPriority(int priority) { return this; }
}
