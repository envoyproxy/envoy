package org.chromium.net.impl;

import android.content.Context;
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl;
import io.envoyproxy.envoymobile.engine.AndroidJniLibrary;
import io.envoyproxy.envoymobile.engine.AndroidNetworkMonitor;
import io.envoyproxy.envoymobile.engine.EnvoyConfiguration;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import io.envoyproxy.envoymobile.engine.EnvoyNativeFilterConfig;
import io.envoyproxy.envoymobile.engine.types.EnvoyEventTracker;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
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
  private String mDnsPreresolveHostnames = "[]";
  private List<String> mDnsFallbackNameservers = Collections.emptyList();
  private boolean mEnableHappyEyeballs = false;
  private boolean mEnableInterfaceBinding = false;
  private int mH2ConnectionKeepaliveIdleIntervalMilliseconds = 100000000;
  private int mH2ConnectionKeepaliveTimeoutSeconds = 10;
  private int mStatsFlushSeconds = 60;
  private int mStreamIdleTimeoutSeconds = 15;
  private int mPerTryIdleTimeoutSeconds = 15;
  private String mAppVersion = "unspecified";
  private String mAppId = "unspecified";
  private String mVirtualClusters = "[]";
  private List<EnvoyHTTPFilterFactory> mPlatformFilterChain = Collections.emptyList();
  private List<EnvoyNativeFilterConfig> mNativeFilterChain = Collections.emptyList();
  private Map<String, EnvoyStringAccessor> mStringAccessors = Collections.emptyMap();

  /**
   * Builder for Native Cronet Engine. Default config enables SPDY, disables QUIC and HTTP cache.
   *
   * @param context Android {@link Context} for engine to use.
   */
  public NativeCronetEngineBuilderImpl(Context context) { super(context); }

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
    return new EnvoyConfiguration(
        mAdminInterfaceEnabled, mGrpcStatsDomain, mStatsDPort, mConnectTimeoutSeconds,
        mDnsRefreshSeconds, mDnsFailureRefreshSecondsBase, mDnsFailureRefreshSecondsMax,
        mDnsQueryTimeoutSeconds, mDnsPreresolveHostnames, mDnsFallbackNameservers,
        mEnableHappyEyeballs, mEnableInterfaceBinding,
        mH2ConnectionKeepaliveIdleIntervalMilliseconds, mH2ConnectionKeepaliveTimeoutSeconds,
        mStatsFlushSeconds, mStreamIdleTimeoutSeconds, mPerTryIdleTimeoutSeconds, mAppVersion,
        mAppId, mVirtualClusters, mNativeFilterChain, mPlatformFilterChain, mStringAccessors);
  }
}
