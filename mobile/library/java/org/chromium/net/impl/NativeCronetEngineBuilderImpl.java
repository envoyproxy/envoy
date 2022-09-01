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
  private Integer mStatsDPort = null;
  private int mConnectTimeoutSeconds = 30;
  private int mDnsRefreshSeconds = 60;
  private int mDnsFailureRefreshSecondsBase = 2;
  private int mDnsFailureRefreshSecondsMax = 10;
  private int mDnsQueryTimeoutSeconds = 25;
  private int mDnsMinRefreshSeconds = 60;
  private String mDnsPreresolveHostnames = "[]";
  private List<String> mDnsFallbackNameservers = Collections.emptyList();
  private boolean mEnableDnsFilterUnroutableFamilies = false;
  private boolean mDnsUseSystemResolver = false;
  private boolean mEnableDrainPostDnsRefresh = false;
  private boolean mEnableGzip = true;
  private boolean mEnableSocketTag = true;
  private boolean mEnableHappyEyeballs = false;
  private boolean mEnableInterfaceBinding = false;
  private int mH2ConnectionKeepaliveIdleIntervalMilliseconds = 100000000;
  private int mH2ConnectionKeepaliveTimeoutSeconds = 10;
  private boolean mH2ExtendKeepaliveTimeout = false;
  private List<String> mH2RawDomains = Collections.emptyList();
  private int mMaxConnectionsPerHost = 7;
  private int mStatsFlushSeconds = 60;
  private int mStreamIdleTimeoutSeconds = 15;
  private int mPerTryIdleTimeoutSeconds = 15;
  private String mAppVersion = "unspecified";
  private String mAppId = "unspecified";
  private TrustChainVerification mTrustChainVerification = VERIFY_TRUST_CHAIN;
  private String mVirtualClusters = "[]";

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
    AndroidEngineImpl engine =
        new AndroidEngineImpl(getContext(), onEngineRunning, mEnvoyLogger, mEnvoyEventTracker);
    AndroidJniLibrary.load(getContext());
    AndroidNetworkMonitor.load(getContext(), engine);
    engine.runWithConfig(createEnvoyConfiguration(), getLogLevel());
    return engine;
  }

  private EnvoyConfiguration createEnvoyConfiguration() {
    List<EnvoyHTTPFilterFactory> platformFilterChain = Collections.emptyList();
    List<EnvoyNativeFilterConfig> nativeFilterChain = new ArrayList<>();
    Map<String, EnvoyStringAccessor> stringAccessors = Collections.emptyMap();
    Map<String, EnvoyKeyValueStore> keyValueStores = Collections.emptyMap();

    return new EnvoyConfiguration(
        mAdminInterfaceEnabled, mGrpcStatsDomain, mStatsDPort, mConnectTimeoutSeconds,
        mDnsRefreshSeconds, mDnsFailureRefreshSecondsBase, mDnsFailureRefreshSecondsMax,
        mDnsQueryTimeoutSeconds, mDnsMinRefreshSeconds, mDnsPreresolveHostnames,
        mDnsFallbackNameservers, mEnableDnsFilterUnroutableFamilies, mDnsUseSystemResolver,
        mEnableDrainPostDnsRefresh, quicEnabled(), mEnableGzip, brotliEnabled(), mEnableSocketTag,
        mEnableHappyEyeballs, mEnableInterfaceBinding,
        mH2ConnectionKeepaliveIdleIntervalMilliseconds, mH2ConnectionKeepaliveTimeoutSeconds,
        mH2ExtendKeepaliveTimeout, mH2RawDomains, mMaxConnectionsPerHost, mStatsFlushSeconds,
        mStreamIdleTimeoutSeconds, mPerTryIdleTimeoutSeconds, mAppVersion, mAppId,
        mTrustChainVerification, mVirtualClusters, nativeFilterChain, platformFilterChain,
        stringAccessors, keyValueStores);
  }
}
