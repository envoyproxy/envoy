package org.chromium.net.impl;

import static io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;

import android.content.Context;
import androidx.annotation.VisibleForTesting;
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
 * Implementation of {@link ICronetEngineBuilder} that builds native Cronet engine.
 */
public class NativeCronetEngineBuilderImpl extends CronetEngineBuilderImpl {

  private final EnvoyLogger mEnvoyLogger = null;
  private final EnvoyEventTracker mEnvoyEventTracker = null;
  private boolean mAdminInterfaceEnabled = false;
  private String mGrpcStatsDomain = null;
  private int mConnectTimeoutSeconds = 30;
  private int mDnsRefreshSeconds = 60;
  private int mDnsFailureRefreshSecondsBase = 2;
  private int mDnsFailureRefreshSecondsMax = 10;
  private int mDnsQueryTimeoutSeconds = 25;
  private int mDnsMinRefreshSeconds = 60;
  private String mDnsPreresolveHostnames = "[]";
  private List<String> mDnsFallbackNameservers = Collections.emptyList();
  private boolean mEnableDnsFilterUnroutableFamilies = true;
  private boolean mDnsUseSystemResolver = true;
  private boolean mEnableDrainPostDnsRefresh = false;
  private boolean mEnableGzip = true;
  private boolean mEnableSocketTag = true;
  private boolean mEnableHappyEyeballs = true;
  private boolean mEnableInterfaceBinding = false;
  private boolean mEnableProxying = false;
  private boolean mEnableSkipDNSLookupForProxiedRequests = false;
  private int mH2ConnectionKeepaliveIdleIntervalMilliseconds = 1;
  private int mH2ConnectionKeepaliveTimeoutSeconds = 10;
  private boolean mH2ExtendKeepaliveTimeout = false;
  private int mMaxConnectionsPerHost = 7;
  private int mStatsFlushSeconds = 60;
  private int mStreamIdleTimeoutSeconds = 15;
  private int mPerTryIdleTimeoutSeconds = 15;
  private String mAppVersion = "unspecified";
  private String mAppId = "unspecified";
  private TrustChainVerification mTrustChainVerification = VERIFY_TRUST_CHAIN;
  private String mVirtualClusters = "[]";
  private boolean mEnablePlatformCertificatesValidation = true;

  /**
   * Builder for Native Cronet Engine. Default config enables SPDY, disables QUIC and HTTP cache.
   *
   * @param context Android {@link Context} for engine to use.
   */
  public NativeCronetEngineBuilderImpl(Context context) { super(context); }

  /**
   * Indicates to skip the TLS certificate verification.
   *
   * @return the builder to facilitate chaining.
   */
  @VisibleForTesting
  public CronetEngineBuilderImpl setMockCertVerifierForTesting() {
    mTrustChainVerification = TrustChainVerification.ACCEPT_UNTRUSTED;
    return this;
  }

  @Override
  public ExperimentalCronetEngine build() {
    if (getUserAgent() == null) {
      setUserAgent(getDefaultUserAgent());
    }
    return new CronetUrlRequestContext(this);
  }

  EnvoyEngine createEngine(EnvoyOnEngineRunning onEngineRunning) {
    AndroidEngineImpl engine = new AndroidEngineImpl(getContext(), onEngineRunning, mEnvoyLogger,
                                                     mEnvoyEventTracker, mEnableProxying);
    AndroidJniLibrary.load(getContext());
    AndroidNetworkMonitor.load(getContext(), engine);
    engine.runWithConfig(createEnvoyConfiguration(), getLogLevel());
    return engine;
  }

  private EnvoyConfiguration createEnvoyConfiguration() {
    List<EnvoyHTTPFilterFactory> platformFilterChain = Collections.emptyList();
    List<EnvoyNativeFilterConfig> nativeFilterChain = Collections.emptyList();
    Map<String, EnvoyStringAccessor> stringAccessors = Collections.emptyMap();
    Map<String, EnvoyKeyValueStore> keyValueStores = Collections.emptyMap();
    List<String> statSinks = Collections.emptyList();

    return new EnvoyConfiguration(
        mAdminInterfaceEnabled, mGrpcStatsDomain, mConnectTimeoutSeconds, mDnsRefreshSeconds,
        mDnsFailureRefreshSecondsBase, mDnsFailureRefreshSecondsMax, mDnsQueryTimeoutSeconds,
        mDnsMinRefreshSeconds, mDnsPreresolveHostnames, mDnsFallbackNameservers,
        mEnableDnsFilterUnroutableFamilies, mDnsUseSystemResolver, mEnableDrainPostDnsRefresh,
        quicEnabled(), mEnableGzip, brotliEnabled(), mEnableSocketTag, mEnableHappyEyeballs,
        mEnableInterfaceBinding, mH2ConnectionKeepaliveIdleIntervalMilliseconds,
        mH2ConnectionKeepaliveTimeoutSeconds, mH2ExtendKeepaliveTimeout, mMaxConnectionsPerHost,
        mStatsFlushSeconds, mStreamIdleTimeoutSeconds, mPerTryIdleTimeoutSeconds, mAppVersion,
        mAppId, mTrustChainVerification, mVirtualClusters, nativeFilterChain, platformFilterChain,
        stringAccessors, keyValueStores, statSinks, mEnableSkipDNSLookupForProxiedRequests,
        mEnablePlatformCertificatesValidation);
  }
}
