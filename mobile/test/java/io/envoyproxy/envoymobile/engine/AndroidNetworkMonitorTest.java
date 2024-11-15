package io.envoyproxy.envoymobile.engine;

import static com.google.common.truth.Truth.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.robolectric.Shadows.shadowOf;

import android.content.Context;
import android.Manifest;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.GrantPermissionRule;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.shadows.ShadowNetwork;
import org.robolectric.shadows.ShadowNetworkCapabilities;

import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;

/**
 * Tests functionality of AndroidNetworkMonitor
 */
@RunWith(RobolectricTestRunner.class)
public class AndroidNetworkMonitorTest {
  @Rule
  public GrantPermissionRule mRuntimePermissionRule =
      GrantPermissionRule.grant(Manifest.permission.ACCESS_NETWORK_STATE);

  private AndroidNetworkMonitor androidNetworkMonitor;
  private ConnectivityManager connectivityManager;
  private NetworkCapabilities networkCapabilities;
  private final EnvoyEngine mockEnvoyEngine = mock(EnvoyEngine.class);

  @Before
  public void setUp() {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    AndroidNetworkMonitor.load(context, mockEnvoyEngine);
    androidNetworkMonitor = AndroidNetworkMonitor.getInstance();
    connectivityManager = androidNetworkMonitor.getConnectivityManager();
    networkCapabilities =
        connectivityManager.getNetworkCapabilities(connectivityManager.getActiveNetwork());
  }

  @After
  public void tearDown() {
    AndroidNetworkMonitor.shutdown();
  }

  /**
   * Tests that isOnline() returns the correct result.
   */
  @Test
  public void testIsOnline() {
    shadowOf(networkCapabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    assertThat(androidNetworkMonitor.isOnline()).isTrue();

    shadowOf(networkCapabilities).removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    assertThat(androidNetworkMonitor.isOnline()).isFalse();
  }

  //=====================================================================================
  // TODO(fredyw): The ShadowConnectivityManager doesn't currently trigger
  // ConnectivityManager.NetworkCallback, so we have to call the callbacks manually. This
  // has been fixed in https://github.com/robolectric/robolectric/pull/9509 but it is
  // not available in the current Roboelectric Shadows framework that we use.
  //=====================================================================================

  @Test
  public void testOnDefaultNetworkAvailable() {
    shadowOf(connectivityManager)
        .getNetworkCallbacks()
        .forEach(callback -> callback.onAvailable(ShadowNetwork.newInstance(0)));

    verify(mockEnvoyEngine).onDefaultNetworkAvailable();
  }

  @Test
  public void testOnDefaultNetworkUnavailable() {
    shadowOf(connectivityManager)
        .getNetworkCallbacks()
        .forEach(callback -> callback.onLost(ShadowNetwork.newInstance(0)));

    verify(mockEnvoyEngine).onDefaultNetworkUnavailable();
  }

  @Test
  public void testOnDefaultNetworkChangedWlan() {
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
      shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
      shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
      callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);
    });

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.WLAN);
  }

  @Test
  public void testOnDefaultNetworkChangedWwan() {
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
      shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
      shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);
      callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);
    });

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.WWAN);
  }

  @Test
  public void testOnDefaultNetworkChangedGeneric() {
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
      shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
      shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET);
      callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);
    });

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.GENERIC);
  }

  @Test
  public void testOnCapabilitiesChangedPreviousNetworkIsEmptyCallbackIsCalledWlan() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.WLAN);
  }

  @Test
  public void testOnCapabilitiesChangedPreviousNetworkIsEmptyCallbackIsCalledWwan() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.WWAN);
  }

  @Test
  public void testOnCapabilitiesChangedPreviousNetworkIsEmptyCallbackIsCalledGeneric() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.GENERIC);
  }

  @Test
  public void testOnCapabilitiesChangedPreviousNetworkNotEmptyCallbackIsCalledWwan() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    callback.transportType = NetworkCapabilities.TRANSPORT_WIFI;
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.WWAN);
  }

  @Test
  public void testOnCapabilitiesChangedPreviousNetworkNotEmptyCallCallbackIsCalledWlan() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    callback.transportType = NetworkCapabilities.TRANSPORT_CELLULAR;
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.WLAN);
  }

  @Test
  public void testOnCapabilitiesChangedPreviousNetworkNotEmptyCallbackIsCalledGeneric() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    callback.transportType = NetworkCapabilities.TRANSPORT_BLUETOOTH;
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_ETHERNET);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine).onDefaultNetworkChanged(EnvoyNetworkType.GENERIC);
  }

  @Test
  public void testOnCapabilitiesChangedPreviousNetworkNotEmptyCallbackIsNotCalled() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    callback.transportType = NetworkCapabilities.TRANSPORT_WIFI;
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine, never()).onDefaultNetworkChanged(any());
  }

  @Test
  public void testOnCapabilitiesChangedNoInternetCallbackIsNotCalled() {
    AndroidNetworkMonitor.DefaultNetworkCallback callback =
        new AndroidNetworkMonitor.DefaultNetworkCallback(mockEnvoyEngine);
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_WIFI);
    callback.onCapabilitiesChanged(ShadowNetwork.newInstance(0), capabilities);

    verify(mockEnvoyEngine, never()).onDefaultNetworkChanged(any());
  }
}
