package org.chromium.net.impl;

import static io.envoyproxy.envoymobile.engine.EnvoyConfiguration.TrustChainVerification.VERIFY_TRUST_CHAIN;

import java.nio.charset.StandardCharsets;
import android.content.Context;
import android.util.Pair;
import androidx.annotation.VisibleForTesting;
import com.google.protobuf.Struct;
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl;
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
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;
import com.google.protobuf.Any;

/**
 * Implementation of {@link ICronetEngineBuilder} that builds native Cronvoy engine.
 */
public class NativeCronvoyEngineBuilderImpl extends CronvoyEngineBuilderImpl {

  // TODO(refactor) move unshared variables into their specific methods.
  private final List<EnvoyNativeFilterConfig> nativeFilterChain = new ArrayList<>();
  private final EnvoyEventTracker mEnvoyEventTracker = null;
  private int mConnectTimeoutSeconds = 10;
  private final int mDnsRefreshSeconds = 60;
  private boolean mDisableDnsRefreshOnFailure = false;
  private boolean mDisableDnsRefreshOnNetworkChange = false;
  private final int mDnsFailureRefreshSecondsBase = 2;
  private final int mDnsFailureRefreshSecondsMax = 10;
  private int mDnsQueryTimeoutSeconds = 5;
  private int mDnsMinRefreshSeconds = 60;
  private final List<String> mDnsPreresolveHostnames = Collections.emptyList();
  private final boolean mEnableDNSCache = false;
  private final int mDnsCacheSaveIntervalSeconds = 1;
  private Optional<Integer> mDnsNumRetries = Optional.empty();
  private final List<String> mDnsFallbackNameservers = Collections.emptyList();
  private final boolean mEnableDnsFilterUnroutableFamilies = true;
  private boolean mEnableDrainPostDnsRefresh = false;
  private final boolean mEnableGzipDecompression = true;
  private final boolean mEnableSocketTag = true;
  private final boolean mEnableInterfaceBinding = false;
  private boolean mEnableProxying = false;
  private final int mH2ConnectionKeepaliveIdleIntervalMilliseconds = 1;
  private final int mH2ConnectionKeepaliveTimeoutSeconds = 10;
  private final int mMaxConnectionsPerHost = 7;
  private int mStreamIdleTimeoutSeconds = 15;
  private int mPerTryIdleTimeoutSeconds = 15;
  private final String mAppVersion = "unspecified";
  private final String mAppId = "unspecified";
  private TrustChainVerification mTrustChainVerification = VERIFY_TRUST_CHAIN;
  private final boolean mEnablePlatformCertificatesValidation = true;
  private String mUpstreamTlsSni = "";
  private int mH3ConnectionKeepaliveInitialIntervalMilliseconds = 0;
  private boolean mUseQuicPlatformPacketWriter = false;
  private boolean mEnableQuicConnectionMigration = false;
  private boolean mMigrateIdleQuicConnection = false;
  private long mMaxIdleTimeBeforeQuicMigrationSeconds = 0;
  private long mMaxTimeOnNonDefaultNetworkSeconds = 0;
  private boolean mUseNetworkChangeEvent = false;
  private boolean mUseV2NetworkMonitor = false;

  private final Map<String, Boolean> mRuntimeGuards = new HashMap<>();

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
   * Set the DNS query timeout, in seconds, which ensures that DNS queries succeed or fail
   * within that time range. See the DnsCacheConfig.dns_query_timeout proto field for details.
   *
   * The default is 5s.
   *
   * @param timeout The DNS query timeout value, in seconds.
   */
  public NativeCronvoyEngineBuilderImpl setDnsQueryTimeoutSeconds(int timeout) {
    mDnsQueryTimeoutSeconds = timeout;
    return this;
  }

  /**
   * Enable Android system proxying.
   *
   * @param enable If true, enable Android proxying; otherwise, don't.
   */
  public NativeCronvoyEngineBuilderImpl setEnableProxying(boolean enable) {
    mEnableProxying = enable;
    return this;
  }

  /**
   * Use a more modern network change API.
   *
   * @param enable If true, use the new API; otherwise, don't.
   */
  public NativeCronvoyEngineBuilderImpl setUseNetworkChangeEvent(boolean use) {
    mUseNetworkChangeEvent = use;
    return this;
  }

  /** Disables the DNS refresh on failure. */
  public NativeCronvoyEngineBuilderImpl
  setDisableDnsRefreshOnFailure(boolean disableDnsRefreshOnFailure) {
    mDisableDnsRefreshOnFailure = disableDnsRefreshOnFailure;
    return this;
  }

  public NativeCronvoyEngineBuilderImpl
  setDisableDnsRefreshOnNetworkChange(boolean disableDnsRefreshOnNetworkChange) {
    mDisableDnsRefreshOnNetworkChange = disableDnsRefreshOnNetworkChange;
    return this;
  }

  public NativeCronvoyEngineBuilderImpl setUseV2NetworkMonitor(boolean useV2NetworkMonitor) {
    mUseV2NetworkMonitor = useV2NetworkMonitor;
    return this;
  }

  /**
   * Set the DNS minimum refresh time, in seconds, which ensures that we wait to refresh a DNS
   * entry for at least the minimum refresh time. For example, if the DNS record TTL is 60 seconds
   * and setMinDnsRefreshSeconds(120) is invoked, then at least 120 seconds will transpire before
   * the DNS entry for a host is refreshed.
   *
   * The default is 60s.
   *
   * @param minRefreshSeconds The DNS minimum refresh time, in seconds.
   */
  public NativeCronvoyEngineBuilderImpl setMinDnsRefreshSeconds(int minRefreshSeconds) {
    mDnsMinRefreshSeconds = minRefreshSeconds;
    return this;
  }

  /**
   * Specifies the number of retries before the resolver gives up. If not specified, the resolver
   * will retry indefinitely until it succeeds or the DNS query times out.
   *
   * @param dnsNumRetries the number of retries
   * @return this builder
   */
  public NativeCronvoyEngineBuilderImpl setDnsNumRetries(int dnsNumRetries) {
    mDnsNumRetries = Optional.of(dnsNumRetries);
    return this;
  }

  /**
   * Set the stream idle timeout, in seconds, which is defined as the period in which there are no
   * active requests. When the idle timeout is reached, the connection is closed.
   *
   * The default is 15s.
   *
   * @param timeout The stream idle timeout, in seconds.
   */
  public NativeCronvoyEngineBuilderImpl setStreamIdleTimeoutSeconds(int timeout) {
    mStreamIdleTimeoutSeconds = timeout;
    return this;
  }

  /**
   * Set the per-try stream idle timeout, in seconds, which is defined as the period in which
   * there are no active requests. When the idle timeout is reached, the connection is closed.
   * This setting is the same as the stream idle timeout, except it's applied per-retry attempt.
   * See
   * https://github.com/envoyproxy/envoy/blob/f15ec821d6a70a1d132f53f50970595efd1b84ee/api/envoy/config/route/v3/route_components.proto#L1570.
   *
   * The default is 15s.
   *
   * @param timeout The per-try idle timeout, in seconds.
   */
  public NativeCronvoyEngineBuilderImpl setPerTryIdleTimeoutSeconds(int timeout) {
    mPerTryIdleTimeoutSeconds = timeout;
    return this;
  }

  /**
   * Adds the boolean value for the reloadable runtime feature flag value. For example, to set the
   * Envoy runtime flag `envoy.reloadable_features.http_allow_partial_urls_in_referer` to true,
   * call `addRuntimeGuard("http_allow_partial_urls_in_referer", true)`.
   *
   * @param feature The reloadable runtime feature flag name.
   * @param value The Boolean value to set the runtime feature flag to.
   */
  public NativeCronvoyEngineBuilderImpl addRuntimeGuard(String feature, boolean value) {
    mRuntimeGuards.put(feature, value);
    return this;
  }

  /**
   * Sets the upstream TLS socket's SNI override. If empty, no SNI override will be configured.
   *
   * @param sni The SNI to override on the upstream HTTP/3 or HTTP/2 TLS socket.
   */
  public NativeCronvoyEngineBuilderImpl setUpstreamTlsSni(String sni) {
    mUpstreamTlsSni = sni;
    return this;
  }

  public NativeCronvoyEngineBuilderImpl
  setH3ConnectionKeepaliveInitialIntervalMilliseconds(int interval) {
    mH3ConnectionKeepaliveInitialIntervalMilliseconds = interval;
    return this;
  }

  /**
   * Set whether to use a platform specific APIs to create UDP socket and the associated QUIC packet
   * writer. Note that `setUseV2NetworkMonitor()` also needs to be called to take effect. This is a
   * temporary API which will be deprecated once the platform specific extension is verified to work
   * and will be used as the default.
   */
  public NativeCronvoyEngineBuilderImpl setUseQuicPlatformPacketWriter(boolean use) {
    mUseQuicPlatformPacketWriter = use;
    return this;
  }

  /**
   * Set whether to enable QUIC connection migration across different network interfaces.
   * Note that `setUseV2NetworkMonitor()` also needs to be called to take effect.
   * If enabled, the engine will automatically be configured to use platform packet writer. *
   */
  public NativeCronvoyEngineBuilderImpl setEnableQuicConnectionMigration(boolean enable) {
    mEnableQuicConnectionMigration = enable;
    return this;
  }

  /**
   * Set whether to migrate idle QUIC connections to a different network upon network events.
   * If not, the connection might be closed or drained or ignore the network event depends on the
   * event type.
   */
  public NativeCronvoyEngineBuilderImpl setMigrateIdleQuicConnection(boolean migrate) {
    mMigrateIdleQuicConnection = migrate;
    return this;
  }

  /**
   * Set the maximum idle time allowed for a QUIC connection before migration.
   */
  public NativeCronvoyEngineBuilderImpl setMaxIdleTimeBeforeQuicMigrationSeconds(long seconds) {
    mMaxIdleTimeBeforeQuicMigrationSeconds = seconds;
    return this;
  }

  /**
   * Set the maximum time a QUIC connection can remain on a non-default network before switching to
   * the default one.
   */
  public NativeCronvoyEngineBuilderImpl setMaxTimeOnNonDefaultNetworkSeconds(long seconds) {
    mMaxTimeOnNonDefaultNetworkSeconds = seconds;
    return this;
  }

  public NativeCronvoyEngineBuilderImpl setConnectTimeoutSeconds(int connectTimeout) {
    mConnectTimeoutSeconds = connectTimeout;
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
    Any anyProto =
        Any.newBuilder()
            .setTypeUrl(
                "type.googleapis.com/envoymobile.test.integration.filters.http.test_read.TestRead")
            .setValue(com.google.protobuf.ByteString.empty())
            .build();
    String config = new String(anyProto.toByteArray(), StandardCharsets.UTF_8);
    nativeFilterChain.add(new EnvoyNativeFilterConfig("envoy.filters.http.test_read", config));
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
    AndroidEngineImpl engine = new AndroidEngineImpl(
        getContext(), onEngineRunning, envoyLogger, mEnvoyEventTracker, mEnableProxying,
        mUseNetworkChangeEvent, mDisableDnsRefreshOnNetworkChange, mUseV2NetworkMonitor);
    engine.runWithConfig(createEnvoyConfiguration(), logLevel);
    return engine;
  }

  private EnvoyConfiguration createEnvoyConfiguration() {
    List<EnvoyHTTPFilterFactory> platformFilterChain = Collections.emptyList();
    Map<String, EnvoyStringAccessor> stringAccessors = Collections.emptyMap();
    Map<String, EnvoyKeyValueStore> keyValueStores = Collections.emptyMap();

    return new EnvoyConfiguration(
        mConnectTimeoutSeconds, mDisableDnsRefreshOnFailure, mDisableDnsRefreshOnNetworkChange,
        mDnsRefreshSeconds, mDnsFailureRefreshSecondsBase, mDnsFailureRefreshSecondsMax,
        mDnsQueryTimeoutSeconds, mDnsMinRefreshSeconds, mDnsPreresolveHostnames, mEnableDNSCache,
        mDnsCacheSaveIntervalSeconds, mDnsNumRetries.orElse(-1), mEnableDrainPostDnsRefresh,
        quicEnabled(), quicConnectionOptions(), quicClientConnectionOptions(), quicHints(),
        quicCanonicalSuffixes(), mEnableGzipDecompression, brotliEnabled(),
        numTimeoutsToTriggerPortMigration(), mEnableSocketTag, mEnableInterfaceBinding,
        mH2ConnectionKeepaliveIdleIntervalMilliseconds, mH2ConnectionKeepaliveTimeoutSeconds,
        mMaxConnectionsPerHost, mStreamIdleTimeoutSeconds, mPerTryIdleTimeoutSeconds, mAppVersion,
        mAppId, mTrustChainVerification, nativeFilterChain, platformFilterChain, stringAccessors,
        keyValueStores, mRuntimeGuards, mEnablePlatformCertificatesValidation, mUpstreamTlsSni,
        mH3ConnectionKeepaliveInitialIntervalMilliseconds,
        mUseQuicPlatformPacketWriter && mUseV2NetworkMonitor,
        mEnableQuicConnectionMigration && mUseV2NetworkMonitor, mMigrateIdleQuicConnection,
        mMaxIdleTimeBeforeQuicMigrationSeconds, mMaxTimeOnNonDefaultNetworkSeconds);
  }
}
