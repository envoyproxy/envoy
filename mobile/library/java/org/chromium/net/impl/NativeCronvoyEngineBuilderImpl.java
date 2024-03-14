package org.chromium.net.impl;

import static io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;

import android.content.Context;
import androidx.annotation.VisibleForTesting;
import com.google.protobuf.Struct;
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl;
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import io.envoyproxy.envoymobile.engine.AndroidNetworkMonitor;
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration;
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import io.envoyproxy.envoymobile.engine.EnvoyNativeFilterConfig;
import io.envoyproxy.envoymobile.engine.types.EnvoyEventTracker;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import io.envoyproxy.envoymobile.engine.types.EnvoyKeyValueStore;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

/**
 * Implementation of {@link ICronetEngineBuilder} that builds native Cronvoy engine.
 */
public class NativeCronvoyEngineBuilderImpl extends CronvoyEngineBuilderImpl {

  // TODO(refactor) move unshared variables into their specific methods.
  private final List<EnvoyNativeFilterConfig> nativeFilterChain = new ArrayList<>();
  private final EnvoyEventTracker mEnvoyEventTracker = null;
  private final int mConnectTimeoutSeconds = 30;
  private final int mDnsRefreshSeconds = 60;
  private final int mDnsFailureRefreshSecondsBase = 2;
  private final int mDnsFailureRefreshSecondsMax = 10;
  private final int mDnsQueryTimeoutSeconds = 25;
  private final int mDnsMinRefreshSeconds = 60;
  private final List<String> mDnsPreresolveHostnames = Collections.emptyList();
  private final boolean mEnableDNSCache = false;
  private final int mDnsCacheSaveIntervalSeconds = 1;
  private final List<String> mDnsFallbackNameservers = Collections.emptyList();
  private final boolean mEnableDnsFilterUnroutableFamilies = true;
  private final boolean mDnsUseSystemResolver = true;
  private boolean mEnableDrainPostDnsRefresh = false;
  private final boolean mEnableGzipDecompression = true;
  private final boolean mEnableSocketTag = true;
  private final boolean mEnableInterfaceBinding = false;
  private final boolean mEnableProxying = false;
  private final int mH2ConnectionKeepaliveIdleIntervalMilliseconds = 1;
  private final int mH2ConnectionKeepaliveTimeoutSeconds = 10;
  private final int mMaxConnectionsPerHost = 7;
  private final int mStreamIdleTimeoutSeconds = 15;
  private final int mPerTryIdleTimeoutSeconds = 15;
  private final String mAppVersion = "unspecified";
  private final String mAppId = "unspecified";
  private TrustChainVerification mTrustChainVerification = VERIFY_TRUST_CHAIN;
  private final boolean mEnablePlatformCertificatesValidation = true;
  private final String mNodeId = "";
  private final String mNodeRegion = "";
  private final String mNodeZone = "";
  private final String mNodeSubZone = "";

  /**
   * Builder for Native Cronet Engine. Default config enables SPDY, disables QUIC and HTTP cache.
   *
   * @param context Android {@link Context} for engine to use.
   */
  public NativeCronvoyEngineBuilderImpl(Context context) { super(context); }

  /**
   * Enable draining of the connections after a DNS refresh changes the host address mapping.
   * The default behavior is to not enable draining post DNS refresh.
   *
   * @param enable If true, enable drain post DNS refresh; otherwise, don't.
   */
  public NativeCronvoyEngineBuilderImpl setEnableDrainPostDnsRefresh(boolean enable) {
    mEnableDrainPostDnsRefresh = enable;
    return this;
  }

  /**
   * Indicates to skip the TLS certificate verification.
   *
   * @return the builder to facilitate chaining.
   */
  @VisibleForTesting
  public CronvoyEngineBuilderImpl setMockCertVerifierForTesting() {
    mTrustChainVerification = TrustChainVerification.ACCEPT_UNTRUSTED;
    return this;
  }

  /**
   * Adds url interceptors to the cronetEngine
   *
   * @return the builder to facilitate chaining.
   */
  @VisibleForTesting
  public CronvoyEngineBuilderImpl addUrlInterceptorsForTesting() {
    nativeFilterChain.add(new EnvoyNativeFilterConfig(
        "envoy.filters.http.test_read",
        "{\"@type\": type.googleapis.com/envoymobile.test.integration.filters.http.test_read.TestRead}"));
    return this;
  }

  @Override
  public ExperimentalCronetEngine build() {
    if (getUserAgent() == null) {
      setUserAgent(getDefaultUserAgent());
    }
    return new CronvoyUrlRequestContext(this);
  }

  EnvoyEngine createEngine(EnvoyOnEngineRunning onEngineRunning, EnvoyLogger envoyLogger,
                           String logLevel) {
    AndroidEngineImpl engine = new AndroidEngineImpl(getContext(), onEngineRunning, envoyLogger,
                                                     mEnvoyEventTracker, mEnableProxying);
    AndroidJniLibrary.load(getContext());
    AndroidNetworkMonitor.load(getContext(), engine);
    engine.runWithConfig(createEnvoyConfiguration(), logLevel);
    return engine;
  }

  private EnvoyConfiguration createEnvoyConfiguration() {
    List<EnvoyHTTPFilterFactory> platformFilterChain = Collections.emptyList();
    Map<String, EnvoyStringAccessor> stringAccessors = Collections.emptyMap();
    Map<String, EnvoyKeyValueStore> keyValueStores = Collections.emptyMap();
    Map<String, Boolean> runtimeGuards = Collections.emptyMap();

    return new EnvoyConfiguration(
        mConnectTimeoutSeconds, mDnsRefreshSeconds, mDnsFailureRefreshSecondsBase,
        mDnsFailureRefreshSecondsMax, mDnsQueryTimeoutSeconds, mDnsMinRefreshSeconds,
        mDnsPreresolveHostnames, mEnableDNSCache, mDnsCacheSaveIntervalSeconds,
        mEnableDrainPostDnsRefresh, quicEnabled(), quicConnectionOptions(),
        quicClientConnectionOptions(), quicHints(), quicCanonicalSuffixes(),
        mEnableGzipDecompression, brotliEnabled(), portMigrationEnabled(), mEnableSocketTag,
        mEnableInterfaceBinding, mH2ConnectionKeepaliveIdleIntervalMilliseconds,
        mH2ConnectionKeepaliveTimeoutSeconds, mMaxConnectionsPerHost, mStreamIdleTimeoutSeconds,
        mPerTryIdleTimeoutSeconds, mAppVersion, mAppId, mTrustChainVerification, nativeFilterChain,
        platformFilterChain, stringAccessors, keyValueStores, runtimeGuards,
        mEnablePlatformCertificatesValidation,
        /*rtdsResourceName=*/"", /*rtdsTimeoutSeconds=*/0, /*xdsAddress=*/"",
        /*xdsPort=*/0, /*xdsGrpcInitialMetadata=*/Collections.emptyMap(),
        /*xdsSslRootCerts=*/"", mNodeId, mNodeRegion, mNodeZone, mNodeSubZone,
        Struct.getDefaultInstance(), /*cdsResourcesLocator=*/"", /*cdsTimeoutSeconds=*/0,
        /*enableCds=*/false);
  }
}
