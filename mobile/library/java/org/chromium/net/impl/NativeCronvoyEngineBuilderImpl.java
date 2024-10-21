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
  private boolean mUseCares = false;
  private final List<Pair<String, Integer>> mCaresFallbackResolvers = new ArrayList<>();
  private boolean mForceV6 = true;
  private boolean mUseGro = false;
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
   * Enable using the c_ares DNS resolver.
   *
   * @param enable If true, use c_ares.
   */
  public NativeCronvoyEngineBuilderImpl setUseCares(boolean enable) {
    mUseCares = enable;
    return this;
  }

  /**
   * Add a fallback resolver to c_cares.
   *
   * @param host ip address string
   * @param port port for the resolver
   */
  public NativeCronvoyEngineBuilderImpl addCaresFallbackResolver(String host, int port) {
    mCaresFallbackResolvers.add(new Pair<String, Integer>(host, port));
    return this;
  }

  /**
   * Set whether to map v4 address to v6.
   *
   * @param enable If true, map v4 address to v6.
   */
  public NativeCronvoyEngineBuilderImpl setForceV6(boolean enable) {
    mForceV6 = enable;
    return this;
  }

  /**
   * Specify whether to use UDP GRO for upstream QUIC/HTTP3 sockets, if GRO is available on the
   * system.
   *
   * @param enable If true, use UDP GRO.
   */
  public NativeCronvoyEngineBuilderImpl setUseGro(boolean enable) {
    mUseGro = enable;
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
    AndroidEngineImpl engine = new AndroidEngineImpl(getContext(), onEngineRunning, envoyLogger,
                                                     mEnvoyEventTracker, mEnableProxying);
    AndroidNetworkMonitor.load(getContext(), engine);
    engine.runWithConfig(createEnvoyConfiguration(), logLevel);
    return engine;
  }

  private EnvoyConfiguration createEnvoyConfiguration() {
    List<EnvoyHTTPFilterFactory> platformFilterChain = Collections.emptyList();
    Map<String, EnvoyStringAccessor> stringAccessors = Collections.emptyMap();
    Map<String, EnvoyKeyValueStore> keyValueStores = Collections.emptyMap();

    return new EnvoyConfiguration(
        mConnectTimeoutSeconds, mDnsRefreshSeconds, mDnsFailureRefreshSecondsBase,
        mDnsFailureRefreshSecondsMax, mDnsQueryTimeoutSeconds, mDnsMinRefreshSeconds,
        mDnsPreresolveHostnames, mEnableDNSCache, mDnsCacheSaveIntervalSeconds,
        mDnsNumRetries.orElse(-1), mEnableDrainPostDnsRefresh, quicEnabled(), mUseCares, mForceV6,
        mUseGro, quicConnectionOptions(), quicClientConnectionOptions(), quicHints(),
        quicCanonicalSuffixes(), mEnableGzipDecompression, brotliEnabled(),
        numTimeoutsToTriggerPortMigration(), mEnableSocketTag, mEnableInterfaceBinding,
        mH2ConnectionKeepaliveIdleIntervalMilliseconds, mH2ConnectionKeepaliveTimeoutSeconds,
        mMaxConnectionsPerHost, mStreamIdleTimeoutSeconds, mPerTryIdleTimeoutSeconds, mAppVersion,
        mAppId, mTrustChainVerification, nativeFilterChain, platformFilterChain, stringAccessors,
        keyValueStores, mRuntimeGuards, mEnablePlatformCertificatesValidation, mUpstreamTlsSni,
        mCaresFallbackResolvers);
  }
}
